// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneEnemyBase.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"

AZoneEnemyBase::AZoneEnemyBase()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(SceneRoot);

	VisualMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	VisualMesh->SetCollisionObjectType(ECC_WorldDynamic);
	VisualMesh->SetCollisionResponseToAllChannels(ECR_Block);
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
