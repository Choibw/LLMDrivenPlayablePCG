// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneEnemyBase.h"

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

bool AZoneEnemyBase::TryAttackPlayer(
	AZonePlayerCharacter* TargetPlayer)
{
	// 자신이나 대상이 죽었으면 공격하지 않음
	if (IsDead()
		|| !IsValid(TargetPlayer)
		|| TargetPlayer->IsDead())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 우선 높이 차이까지 포함한 중심 간 거리로 검사
	const float SafeAttackRange = FMath::Max(AttackRange, 1.0f);

	const float DistanceSquared = FVector::DistSquared(
		GetActorLocation(),
		TargetPlayer->GetActorLocation()
	);

	if (DistanceSquared > FMath::Square(SafeAttackRange))
	{
		return false;
	}

	// 공격 간격 검사
	const double CurrentTime = World->GetTimeSeconds();
	const float SafeAttackInterval =
		FMath::Max(AttackInterval, 0.1f);

	if (CurrentTime - LastAttackTime < SafeAttackInterval)
	{
		return false;
	}

	// 적과 플레이어 사이에 장애물이 있으면 공격하지 않음
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(TargetPlayer);

	FHitResult HitResult;

	const bool bBlocked = World->LineTraceSingleByChannel(
		HitResult,
		GetActorLocation(),
		TargetPlayer->GetActorLocation(),
		ECC_Visibility,
		Params
	);

	if (bBlocked)
	{
		return false;
	}

	if (!FMath::IsFinite(AttackDamage) || AttackDamage <= 0.0f)
	{
		return false;
	}

	// 실제 공격이 성립한 시각 기록
	LastAttackTime = CurrentTime;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[EnemyAttack] Attack. Enemy=%s Player=%s Damage=%.1f"),
		*GetName(),
		*TargetPlayer->GetName(),
		AttackDamage
	);

	TargetPlayer->ApplyDamage(AttackDamage);

	return true;
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
