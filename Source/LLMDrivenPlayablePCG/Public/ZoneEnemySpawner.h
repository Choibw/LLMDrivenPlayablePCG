// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZoneEnemySpawner.generated.h"

class AZoneEnemyBase;
class UStaticMeshComponent;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneEnemySpawner : public AActor
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCombatAreaCleared, AZoneEnemySpawner*);

	FOnCombatAreaCleared OnCombatAreaCleared;

public:
	AZoneEnemySpawner();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SpawnEnemies();
	void ClearSpawnedEnemies();
	FVector GetRandomSpawnLocation() const;

	void HandleEnemyDied(AZoneEnemyBase* DeadEnemy);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> DebugMesh;

	UPROPERTY(EditAnywhere, Category = "Enemy")
	TSubclassOf<AZoneEnemyBase> EnemyClass;

	UPROPERTY(EditAnywhere, Category = "Enemy")
	int32 EnemyCount = 1;

	UPROPERTY(EditAnywhere, Category = "Enemy")
	float SpawnRadius = 300.0f;

	UPROPERTY()
	TArray<TObjectPtr<AZoneEnemyBase>> SpawnedEnemies;
};
