// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ZoneEnemyAIController.generated.h"

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AZoneEnemyAIController();

    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void OnPossess(APawn* InPawn) override;

    // 처음 플레이어를 감지하는 거리
    UPROPERTY(EditDefaultsOnly, Category = "AI",
        meta = (ClampMin = "0.0"))
    float DetectionRadius = 1000.0f;

    // 감지한 플레이어를 놓치는 거리
    UPROPERTY(EditDefaultsOnly, Category = "AI",
        meta = (ClampMin = "0.0"))
    float LoseTargetRadius = 900.0f;

private:
    bool bPlayerDetected = false;

    // 각 적이 처음 생성된 위치
    FVector HomeLocation = FVector::ZeroVector;

    // 복귀를 마치면 되돌릴 원래 이동 속도
    float NormalMoveSpeed = 250.0f;

    // 현재 복귀 중인지 여부
    bool bReturningHome = false;

    UPROPERTY(EditDefaultsOnly, Category = "AI",
        meta = (ClampMin = "0.0"))
    float MaxChaseDistanceFromHome = 1200.0f;

    UPROPERTY(EditDefaultsOnly, Category = "AI",
        meta = (ClampMin = "1.0"))
    float ReturnMoveSpeed = 500.0f;
};