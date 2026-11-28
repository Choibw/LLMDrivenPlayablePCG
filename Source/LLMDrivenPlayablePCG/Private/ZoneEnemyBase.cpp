// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneEnemyBase.h"

#include "Animation/AnimMontage.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "ZonePlayerCharacter.h"
#include "Engine/World.h"

AZoneEnemyBase::AZoneEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// 이동과 바닥 충돌은 캡슐이 담당
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 88.0f);
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));

	// 플레이어의 공격 트레이스는 외형 메시에서 판정
	GetCapsuleComponent()->SetCollisionResponseToChannel(
		ECC_Visibility,
		ECR_Ignore
	);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(
		TEXT("VisualMesh")
	);
	VisualMesh->SetupAttachment(GetCapsuleComponent());

	// 메시의 물리 시뮬레이션은 사용하지 않음
	VisualMesh->SetSimulatePhysics(false);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VisualMesh->SetCollisionObjectType(ECC_WorldDynamic);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	VisualMesh->SetCollisionResponseToChannel(
		ECC_Visibility,
		ECR_Block
	);

	// 이후 이동 AI에서 사용할 기본 설정
	bUseControllerRotationYaw = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate =
		FRotator(0.0f, 360.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = 250.0f;
}

void AZoneEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = MaxHealth;

	UE_LOG(LogTemp, Log, TEXT("[ZoneEnemyBase] Enemy initialized. Name=%s MaxHealth=%f"),
		*GetName(),
		MaxHealth);

	if (bDebugAutoDie)
	{
		GetWorldTimerManager().SetTimer(
			DebugAutoDieTimerHandle,
			this,
			&AZoneEnemyBase::DebugAutoDie,
			DebugAutoDieDelay,
			false
		);

		UE_LOG(LogTemp, Log, TEXT("[ZoneEnemyBase] Debug auto die scheduled. Name=%s Delay=%f"),
			*GetName(),
			DebugAutoDieDelay);
	}
}

void AZoneEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DebugAutoDieTimerHandle);

	Super::EndPlay(EndPlayReason);
}

void AZoneEnemyBase::ApplyDamage(float DamageAmount)
{
	if (DamageAmount <= 0.0f)
	{
		return;
	}

	if (IsDead())
	{
		return;
	}

	CurrentHealth -= DamageAmount;

	UE_LOG(LogTemp, Log, TEXT("[ZoneEnemyBase] Enemy damaged. Name=%s Damage=%f CurrentHealth=%f"),
		*GetName(),
		DamageAmount,
		CurrentHealth);

	if (CurrentHealth <= 0.0f)
	{
		CurrentHealth = 0.0f;
		Die();
	}
}

bool AZoneEnemyBase::IsDead() const
{
	return CurrentHealth <= 0.0f;
}

bool AZoneEnemyBase::TryAttackPlayer(AZonePlayerCharacter* TargetPlayer)
{
	UWorld* World = GetWorld();
	if (!World || !CanHitPlayer(TargetPlayer))
	{
		return false;
	}

	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime - LastAttackTime < FMath::Max(AttackInterval, 0.1f))
	{
		return false;
	}

	if (UAnimMontage* Montage = AttackMontage.Get())
	{
		if (GetCurrentMontage() == Montage || PlayAnimMontage(Montage) <= 0.0f)
		{
			return false;
		}

		const FVector ToPlayer = TargetPlayer->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.0, ToPlayer.Rotation().Yaw, 0.0));
		PendingAttackTarget = TargetPlayer;
	}
	else
	{
		ApplyAttackDamageTo(TargetPlayer);
	}

	LastAttackTime = CurrentTime;
	return true;
}

void AZoneEnemyBase::ResolveAttackHit()
{
	AZonePlayerCharacter* TargetPlayer = PendingAttackTarget.Get();
	PendingAttackTarget.Reset();

	if (!CanHitPlayer(TargetPlayer))
	{
		return;
	}

	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const FVector ToPlayer = TargetPlayer->GetActorLocation() - GetActorLocation();
	if (FVector::DotProduct(Forward, ToPlayer) >= 0.0)
	{
		ApplyAttackDamageTo(TargetPlayer);
	}
}

bool AZoneEnemyBase::CanHitPlayer(AZonePlayerCharacter* TargetPlayer) const
{
	if (IsDead() || !IsValid(TargetPlayer) || TargetPlayer->IsDead()
		|| !FMath::IsFinite(AttackDamage) || AttackDamage <= 0.0f)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Origin = GetActorLocation();
	const FVector TargetLocation = TargetPlayer->GetActorLocation();
	if (FVector::DistSquared(Origin, TargetLocation) > FMath::Square(GetAttackRange()))
	{
		return false;
	}

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(TargetPlayer);

	FHitResult HitResult;
	return !World->LineTraceSingleByChannel(
		HitResult, Origin, TargetLocation, ECC_Visibility, Params
	);
}

void AZoneEnemyBase::ApplyAttackDamageTo(AZonePlayerCharacter* TargetPlayer)
{
	UE_LOG(LogTemp, Log,
		TEXT("[EnemyAttack] Attack. Enemy=%s Player=%s Damage=%.1f"),
		*GetName(), *TargetPlayer->GetName(), AttackDamage);

	TargetPlayer->ApplyDamage(AttackDamage);
}

void AZoneEnemyBase::Die()
{
	UE_LOG(LogTemp, Log, TEXT("[ZoneEnemyBase] Enemy died. Name=%s"),
		*GetName());

	OnEnemyDied.Broadcast(this);

	Destroy();
}

void AZoneEnemyBase::DebugAutoDie()
{
	if (IsDead())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[ZoneEnemyBase] Debug auto die triggered. Name=%s"),
		*GetName());

	ApplyDamage(MaxHealth + 9999.0f);
}
