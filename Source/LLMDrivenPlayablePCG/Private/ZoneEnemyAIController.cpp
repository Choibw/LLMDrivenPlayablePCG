// Fill out your copyright notice in the Description page of Project Settings.


#include "ZoneEnemyAIController.h"

#include "ZoneEnemyBase.h"
#include "ZonePlayerCharacter.h"
#include "Kismet/GameplayStatics.h"

#include "GameFramework/CharacterMovementComponent.h"

#include "Navigation/PathFollowingComponent.h"

AZoneEnemyAIController::AZoneEnemyAIController()
{
    PrimaryActorTick.bCanEverTick = true;

    // 매 프레임 대신 0.2초마다 감지 상태 확인
    PrimaryActorTick.TickInterval = 0.2f;
}

void AZoneEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    bPlayerDetected = false;
    bReturningHome = false;

    if (IsValid(InPawn))
    {
        HomeLocation = InPawn->GetActorLocation();
    }

    if (AZoneEnemyBase* Enemy = Cast<AZoneEnemyBase>(InPawn))
    {
        NormalMoveSpeed =
            Enemy->GetCharacterMovement()->MaxWalkSpeed;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[EnemyAI] Controller connected. Enemy=%s Home=%s"),
        *GetNameSafe(InPawn),
        *HomeLocation.ToString()
    );
}

void AZoneEnemyAIController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // 1. 제어 중인 적 확인
    AZoneEnemyBase* Enemy = Cast<AZoneEnemyBase>(GetPawn());

    if (!IsValid(Enemy) || Enemy->IsDead())
    {
        StopMovement();

        bPlayerDetected = false;
        bReturningHome = false;

        return;
    }

    UCharacterMovementComponent* Movement =
        Enemy->GetCharacterMovement();

    if (!Movement)
    {
        StopMovement();
        return;
    }

    const FVector EnemyLocation = Enemy->GetActorLocation();

    const float DistanceFromHomeSquared = FVector::DistSquared2D(
        EnemyLocation,
        HomeLocation
    );

    // 2. 복귀 중이면 복귀만 처리
    if (bReturningHome)
    {
        const float HomeArrivalRadius = 150.0f;

        if (DistanceFromHomeSquared
            <= FMath::Square(HomeArrivalRadius))
        {
            StopMovement();

            bReturningHome = false;
            bPlayerDetected = false;

            Movement->MaxWalkSpeed = NormalMoveSpeed;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[EnemyAI] Return completed. Enemy=%s"),
                *Enemy->GetName()
            );
        }
        else if (GetMoveStatus() == EPathFollowingStatus::Idle)
        {
            MoveToLocation(
                HomeLocation,
                50.0f,
                false,
                true,
                true,
                false,
                nullptr,
                false
            );
        }

        // 복귀 중에는 플레이어 감지·공격을 하지 않음
        return;
    }

    // 3. 살아 있는 플레이어 확인 및 감지 거리 검사
    AZonePlayerCharacter* Player = Cast<AZonePlayerCharacter>(
        UGameplayStatics::GetPlayerCharacter(this, 0)
    );

    const bool bPlayerAlive =
        IsValid(Player) && !Player->IsDead();

    bool bShouldDetectPlayer = false;

    if (bPlayerAlive)
    {
        const float DistanceToPlayerSquared = FVector::DistSquared2D(
            EnemyLocation,
            Player->GetActorLocation()
        );

        const float SafeDetectionRadius =
            FMath::Max(DetectionRadius, 0.0f);

        const float SafeLoseTargetRadius =
            FMath::Max(LoseTargetRadius, SafeDetectionRadius);

        const float CheckRadius = bPlayerDetected
            ? SafeLoseTargetRadius
            : SafeDetectionRadius;

        bShouldDetectPlayer =
            DistanceToPlayerSquared <= FMath::Square(CheckRadius);
    }

    // 4. 추적을 포기하고 복귀해야 하는지 확인
    const float SafeMaxChaseDistance =
        FMath::Max(MaxChaseDistanceFromHome, 0.0f);

    const bool bTooFarFromHome =
        DistanceFromHomeSquared
        > FMath::Square(SafeMaxChaseDistance);

        const bool bLostPlayer =
            bPlayerDetected && !bShouldDetectPlayer;

        if (bLostPlayer || bTooFarFromHome)
        {
            StopMovement();

            bPlayerDetected = false;
            bReturningHome = true;

            Movement->MaxWalkSpeed =
                FMath::Max(ReturnMoveSpeed, 1.0f);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[EnemyAI] Returning home. Enemy=%s Reason=%s"),
                *Enemy->GetName(),
                bTooFarFromHome
                ? TEXT("Home distance limit")
                : TEXT("Player lost")
            );

            // 다음 Tick에서 복귀 이동 시작
            return;
        }

        // 5. 새로 감지했을 때 로그 출력
        if (bShouldDetectPlayer && !bPlayerDetected)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[EnemyAI] Player detected. Enemy=%s Player=%s"),
                *Enemy->GetName(),
                *GetNameSafe(Player)
            );
        }

        bPlayerDetected = bShouldDetectPlayer;

        // 6. 감지한 대상이 없으면 대기
        if (!bPlayerDetected || !bPlayerAlive)
        {
            StopMovement();
            return;
        }

        // 7. 공격 사거리 검사: 높이 차이도 포함
        const float DistanceToPlayerSquared = FVector::DistSquared(
            EnemyLocation,
            Player->GetActorLocation()
        );

        const float AttackRange = Enemy->GetAttackRange();

        if (DistanceToPlayerSquared <= FMath::Square(AttackRange))
        {
            StopMovement();

            // 공격 간격과 장애물 검사는 적의 공격 함수에서 처리
            Enemy->TryAttackPlayer(Player);

            return;
        }

        // 8. 공격 거리 밖이면 추적
        if (GetMoveStatus() == EPathFollowingStatus::Idle)
        {
            MoveToActor(
                Player,
                120.0f,
                true,
                true,
                false,
                nullptr,
                true
            );
        }
}
