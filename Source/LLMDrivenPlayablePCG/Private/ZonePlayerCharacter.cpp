// Fill out your copyright notice in the Description page of Project Settings.

#include "ZonePlayerCharacter.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "GameFramework/RootMotionSource.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

#include "EnhancedInputComponent.h"
#include "InputActionValue.h"

#include "ZoneEnemyBase.h"
#include "DrawDebugHelpers.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#include "PCGZoneController.h"
#include "ZonePlayerController.h"
#include "TimerManager.h"

#include "ZoneDialogueNPC.h"
#include "EngineUtils.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"

AZonePlayerCharacter::AZonePlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
}

void AZonePlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	MaxHealth = FMath::Max(MaxHealth, 1.0f);
	CurrentHealth = MaxHealth;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerHealth] Initialized. Health=%.1f / %.1f"),
		CurrentHealth,
		MaxHealth
	);
}

void AZonePlayerCharacter::ApplyDamage(float DamageAmount)
{
	// 잘못된 피해량이나 이미 체력이 소진된 상태는 무시
	if (!FMath::IsFinite(DamageAmount)
		|| DamageAmount <= 0.0f
		|| bIsDead)
	{
		return;
	}

	CurrentHealth = FMath::Clamp(
		CurrentHealth - DamageAmount,
		0.0f,
		MaxHealth
	);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerHealth] Damaged. Damage=%.1f Health=%.1f / %.1f"),
		DamageAmount,
		CurrentHealth,
		MaxHealth
	);

	if (CurrentHealth <= 0.0f)
	{
		Die();
	}
}

bool AZonePlayerCharacter::RespawnAtLocation(const FVector& RespawnLocation)
{
	// 살아 있으면 부활 처리하지 않음
	if (!bIsDead)
	{
		return false;
	}

	APlayerController* PlayerController =
		Cast<APlayerController>(GetController());

	if (!PlayerController)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PlayerHealth] Respawn failed: no PlayerController.")
		);

		return false;
	}

	// 지정 위치로 이동. 충돌 때문에 이동할 수 없으면 부활 중단
	const bool bMoved = TeleportTo(
		RespawnLocation,
		GetActorRotation(),
		false,
		false
	);

	if (!bMoved)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PlayerHealth] Respawn failed: destination blocked. Location=%s"),
			*RespawnLocation.ToString()
		);

		return false;
	}

	// 별도 호출로 먼저 부활한 경우에도 예약된 타이머 정리
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	// 체력과 생존 상태 복구
	CurrentHealth = MaxHealth;
	bIsDead = false;

	// 사망 시 중단했던 이동 기능 복구
	StopJumping();

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);

	// 사망 시 비활성화했던 입력 복구
	EnableInput(PlayerController);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[PlayerHealth] Respawned. Health=%.1f / %.1f Location=%s"),
		CurrentHealth,
		MaxHealth,
		*GetActorLocation().ToString()
	);

	return true;
}

void AZonePlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent)
	{
		return;
	}

	if (MoveAction)
	{
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AZonePlayerCharacter::Move);

		EnhancedInputComponent->BindActionValue(MoveAction);
	}

	if (LookAction)
	{
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AZonePlayerCharacter::Look);
	}

	if (AttackAction)
	{
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AZonePlayerCharacter::Attack);
	}

	if (JumpAction)
	{
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AZonePlayerCharacter::TryJump);

		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}

	if (InteractAction)
	{
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AZonePlayerCharacter::Interact);
	}

	if (DodgeAction)
	{
		EnhancedInputComponent->BindAction(DodgeAction.Get(), ETriggerEvent::Started, this, &AZonePlayerCharacter::Dodge);
	}
}

void AZonePlayerCharacter::Move(const FInputActionValue& Value)
{
	if (bIsDodging)
	{
		return;
	}

	const FVector2D MovementVector = Value.Get<FVector2D>();

	if (!Controller)
	{
		return;
	}

	const FRotator ControlRotation = Controller->GetControlRotation();
	const FRotator YawRotation(0.0f, ControlRotation.Yaw, 0.0f);

	const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(ForwardDirection, MovementVector.Y);
	AddMovementInput(RightDirection, MovementVector.X);
}

void AZonePlayerCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();

	AddControllerYawInput(LookAxisVector.X);
	AddControllerPitchInput(LookAxisVector.Y);
}

void AZonePlayerCharacter::Attack()
{
	if (bIsDead || bIsDodging || !AttackMontage)
	{
		return;
	}

	const UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance || AnimInstance->Montage_IsActive(AttackMontage.Get()))
	{
		return;
	}

	bSwordHitPending = PlayAnimMontage(AttackMontage.Get()) > 0.0f;
}

void AZonePlayerCharacter::ResolveSwordHit()
{
	if (bIsDead || bIsDodging || !bSwordHitPending)
	{
		return;
	}

	bSwordHitPending = false;
	PerformAttackTrace();
}

void AZonePlayerCharacter::Dodge()
{
	if (bIsDead || bIsDodging || !DodgeMontage)
	{
		return;
	}

	constexpr float RollPlayRate = 1.8f;
	constexpr float RollDistance = 360.0f;  // cm
	constexpr float RollMoveTime = 0.45f;   // 초

	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (!AnimInstance || PlayAnimMontage(DodgeMontage.Get(), RollPlayRate) <= 0.0f)
	{
		return;
	}

	bIsDodging = true;
	bSwordHitPending = false;
	ConsumeMovementInputVector();

	FVector DodgeDirection = GetActorForwardVector().GetSafeNormal2D();

	const UEnhancedInputComponent* EnhancedInput =
		Cast<UEnhancedInputComponent>(InputComponent);

	if (EnhancedInput && MoveAction && Controller)
	{
		const FVector2D Stick =
			EnhancedInput->GetBoundActionValue(MoveAction).Get<FVector2D>();

		if (!Stick.IsNearlyZero())
		{
			const FRotator CameraYaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
			DodgeDirection = CameraYaw.RotateVector(
				FVector(Stick.Y, Stick.X, 0.0f)).GetSafeNormal();
		}
	}

	SetActorRotation(DodgeDirection.Rotation());

	auto RollMotion = MakeShared<FRootMotionSource_ConstantForce>();
	RollMotion->InstanceName = FName(TEXT("PlayerDodgeForward"));
	RollMotion->AccumulateMode = ERootMotionAccumulateMode::Override;
	RollMotion->Force = DodgeDirection * (RollDistance / RollMoveTime);
	RollMotion->Duration = RollMoveTime;
	RollMotion->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	RollMotion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	RollMotion->FinishVelocityParams.ClampVelocity = 0.0f;
	GetCharacterMovement()->ApplyRootMotionSource(RollMotion);

	FOnMontageEnded EndDelegate;
	EndDelegate.BindUObject(this, &AZonePlayerCharacter::OnDodgeMontageEnded);
	AnimInstance->Montage_SetEndDelegate(EndDelegate, DodgeMontage.Get());
}

void AZonePlayerCharacter::TryJump()
{
	if (bIsDead || bIsDodging)
	{
		return;
	}

	const UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && AttackMontage &&
		AnimInstance->GetInstanceForMontage(AttackMontage.Get()))
	{
		return;
	}

	Jump();
}

void AZonePlayerCharacter::OnDodgeMontageEnded(UAnimMontage*, bool)
{
	GetCharacterMovement()->RemoveRootMotionSource(FName(TEXT("PlayerDodgeForward")));
	bIsDodging = false;
}

void AZonePlayerCharacter::PerformAttackTrace()
{
	constexpr float HitRadius = 230.0f;
	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();

	const TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes = {
		UEngineTypes::ConvertToObjectType(ECC_Pawn)
	};

	TArray<AActor*> NearbyEnemies;
	UKismetSystemLibrary::SphereOverlapActors(
		this, Origin, HitRadius, ObjectTypes,
		AZoneEnemyBase::StaticClass(), TArray<AActor*>{this}, NearbyEnemies
	);

	for (AActor* Actor : NearbyEnemies)
	{
		AZoneEnemyBase* Enemy = Cast<AZoneEnemyBase>(Actor);
		if (!IsValid(Enemy))
		{
			continue;
		}

		const FVector ToEnemy = Enemy->GetActorLocation() - Origin;
		if (FVector::DotProduct(Forward, ToEnemy) >= 0.0f)
		{
			Enemy->ApplyDamage(AttackDamage);
		}
	}
}

void AZonePlayerCharacter::Die()
{
	// 사망 처리가 중복 실행되지 않도록 방지
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;

	StopJumping();

	// 현재 움직임을 멈추고 이동 기능 비활성화
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	// 이동·시점·점프·공격 입력 중단
	if (APlayerController* PlayerController =
		Cast<APlayerController>(GetController()))
	{
		DisableInput(PlayerController);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[PlayerHealth] Player died.")
	);

	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle,
		this,
		&AZonePlayerCharacter::RequestRespawn,
		FMath::Max(RespawnDelay, 0.1f),
		false
	);
}

void AZonePlayerCharacter::Interact()
{
	if (bIsDead || !GetWorld())
	{
		return;
	}

	AZoneDialogueNPC* ClosestNPC = nullptr;
	double ClosestDistanceSquared =
		TNumericLimits<double>::Max();

	// 대화 가능한 NPC 중 가장 가까운 NPC 선택
	for (TActorIterator<AZoneDialogueNPC> It(GetWorld()); It; ++It)
	{
		AZoneDialogueNPC* NPC = *It;

		if (!IsValid(NPC) || !NPC->CanInteract(this))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(
			GetActorLocation(),
			NPC->GetActorLocation()
		);

		if (DistanceSquared < ClosestDistanceSquared)
		{
			ClosestDistanceSquared = DistanceSquared;
			ClosestNPC = NPC;
		}
	}

	if (!IsValid(ClosestNPC))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("[Dialogue] No nearby NPC.")
		);

		return;
	}

	AZonePlayerController* PlayerController =
		Cast<AZonePlayerController>(GetController());

	if (!IsValid(PlayerController))
	{
		return;
	}

	// 위치 이동, 입력 차단, 카메라 전환은 컨트롤러에서 처리
	PlayerController->BeginNPCDialogue(ClosestNPC);
}

void AZonePlayerCharacter::RequestRespawn()
{
	// 다른 경로로 이미 부활했다면 처리하지 않음
	if (!bIsDead)
	{
		return;
	}

	APCGZoneController* ZoneController =
		Cast<APCGZoneController>(
			UGameplayStatics::GetActorOfClass(
				this,
				APCGZoneController::StaticClass()
			)
		);

	if (!IsValid(ZoneController))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PlayerHealth] Respawn failed: ZoneController not found.")
		);

		return;
	}

	if (!ZoneController->RespawnPlayerAtSpawnPoint())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[PlayerHealth] Respawn request failed.")
		);
	}
}

void AZonePlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	Super::EndPlay(EndPlayReason);
}
