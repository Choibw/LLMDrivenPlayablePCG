// Fill out your copyright notice in the Description page of Project Settings.

#include "ZonePlayerCharacter.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/Controller.h"

#include "ZoneEnemyBase.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"

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
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);

		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	}
}

void AZonePlayerCharacter::Move(const FInputActionValue& Value)
{
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
	if (bIsDead)
	{
		return;
	}

	PerformAttackTrace();
}

void AZonePlayerCharacter::PerformAttackTrace()
{
	AController* OwnerController = GetController();
	if (!OwnerController)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	OwnerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + ViewRotation.Vector() * AttackRange;

	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		HitResult,
		TraceStart,
		TraceEnd,
		ECC_Visibility,
		Params
	);

	DrawDebugLine(
		GetWorld(),
		TraceStart,
		TraceEnd,
		bHit ? FColor::Green : FColor::Red,
		false,
		1.0f,
		0,
		2.0f
	);

	if (!bHit)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PlayerAttack] Miss."));
		return;
	}

	AActor* HitActor = HitResult.GetActor();

	UE_LOG(LogTemp, Warning, TEXT("[PlayerAttack] Hit Actor=%s"), *GetNameSafe(HitActor));

	AZoneEnemyBase* HitEnemy = Cast<AZoneEnemyBase>(HitActor);
	if (!HitEnemy)
	{
		return;
	}

	HitEnemy->ApplyDamage(AttackDamage);

	UE_LOG(LogTemp, Warning, TEXT("[PlayerAttack] Enemy damaged. Target=%s Damage=%f"),
		*GetNameSafe(HitEnemy),
		AttackDamage
	);
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
}
