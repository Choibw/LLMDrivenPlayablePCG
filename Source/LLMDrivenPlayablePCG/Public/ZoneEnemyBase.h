// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ZoneEnemyBase.generated.h"

class UStaticMeshComponent;
class AZonePlayerCharacter;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnZoneEnemyDied, AZoneEnemyBase*);

	FOnZoneEnemyDied OnEnemyDied;

public:
	AZoneEnemyBase();

	void ApplyDamage(float DamageAmount);
	bool IsDead() const;

	// 실제 피해가 적용되면 true 반환
	bool TryAttackPlayer(AZonePlayerCharacter* TargetPlayer);

	float GetAttackRange() const
	{
		return FMath::Max(AttackRange, 1.0f);
	}

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void Die();

protected:
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

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Combat",
		meta = (ClampMin = "0.0"))
	float AttackDamage = 10.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat",
		meta = (ClampMin = "1.0"))
	float AttackRange = 250.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Combat",
		meta = (ClampMin = "0.1"))
	float AttackInterval = 1.0f;

private:
	// 첫 공격은 즉시 가능하도록 충분히 과거 시각으로 초기화
	double LastAttackTime = -1000000.0;
};
