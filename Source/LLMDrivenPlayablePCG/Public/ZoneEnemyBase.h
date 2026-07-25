// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZoneEnemyBase.generated.h"

class UStaticMeshComponent;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneEnemyBase : public AActor
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnZoneEnemyDied, AZoneEnemyBase*);

	FOnZoneEnemyDied OnEnemyDied;

public:
	AZoneEnemyBase();

	void ApplyDamage(float DamageAmount);
	bool IsDead() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Die();

protected:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(EditAnywhere, Category = "Enemy")
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleAnywhere, Category = "Enemy")
	float CurrentHealth = 0.0f;

private:
	FTimerHandle DebugAutoDieTimerHandle;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDebugAutoDie = false;

	UPROPERTY(EditAnywhere, Category = "Debug", meta = (EditCondition = "bDebugAutoDie"))
	float DebugAutoDieDelay = 10.0f;

private:
	void DebugAutoDie();
};
