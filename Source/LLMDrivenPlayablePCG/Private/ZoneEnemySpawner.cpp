// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneEnemySpawner.h"

#include "ZoneEnemyBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AZoneEnemySpawner::AZoneEnemySpawner()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DebugMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DebugMesh"));
	DebugMesh->SetupAttachment(SceneRoot);

	DebugMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AZoneEnemySpawner::BeginPlay()
{
	Super::BeginPlay();

	SpawnEnemies();
}

void AZoneEnemySpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSpawnedEnemies();

	Super::EndPlay(EndPlayReason);
}

void AZoneEnemySpawner::SpawnEnemies()
{
	if (!EnemyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ZoneEnemySpawner] EnemyClass is not set. Spawner=%s"),
			*GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 SafeEnemyCount = FMath::Max(EnemyCount, 0);

	for (int32 i = 0; i < SafeEnemyCount; ++i)
	{
		const FVector SpawnLocation = GetRandomSpawnLocation();
		const FRotator SpawnRotation = GetActorRotation();

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;

		AZoneEnemyBase* SpawnedEnemy = World->SpawnActor<AZoneEnemyBase>(
			EnemyClass,
			SpawnLocation,
			SpawnRotation,
			SpawnParams
		);

		if (IsValid(SpawnedEnemy))
		{
			SpawnedEnemy->OnEnemyDied.AddUObject(this, &AZoneEnemySpawner::HandleEnemyDied);

			SpawnedEnemies.Add(SpawnedEnemy);

			UE_LOG(LogTemp, Log, TEXT("[ZoneEnemySpawner] Spawned enemy. Index=%d Location=%s"),
				i,
				*SpawnLocation.ToString());
		}
	}
}

void AZoneEnemySpawner::ClearSpawnedEnemies()
{
	for (AZoneEnemyBase* Enemy : SpawnedEnemies)
	{
		if (IsValid(Enemy))
		{
			Enemy->Destroy();
		}
	}

	SpawnedEnemies.Empty();
}

FVector AZoneEnemySpawner::GetRandomSpawnLocation() const
{
	if (SpawnRadius <= 0.0f)
	{
		return GetActorLocation();
	}

	const FVector2D RandomOffset2D = FMath::RandPointInCircle(SpawnRadius);

	return GetActorLocation() + FVector(RandomOffset2D.X, RandomOffset2D.Y, 0.0f);
}

void AZoneEnemySpawner::HandleEnemyDied(AZoneEnemyBase* DeadEnemy)
{
	if (!DeadEnemy)
	{
		return;
	}

	SpawnedEnemies.Remove(DeadEnemy);

	UE_LOG(LogTemp, Log, TEXT("[ZoneEnemySpawner] Enemy died detected. Enemy=%s Remaining=%d"),
		*DeadEnemy->GetName(),
		SpawnedEnemies.Num());

	if (SpawnedEnemies.Num() == 0)
	{
		UE_LOG(LogTemp, Log, TEXT("[ZoneEnemySpawner] Combat area cleared. Spawner=%s"),
			*GetName());

		OnCombatAreaCleared.Broadcast(this);
	}
}
