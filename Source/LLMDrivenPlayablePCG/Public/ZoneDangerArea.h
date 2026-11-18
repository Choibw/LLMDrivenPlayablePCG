// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "ZoneDangerArea.generated.h"

class AZonePlayerCharacter;
class UCapsuleComponent;
class UDecalComponent;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneDangerArea : public AActor
{
    GENERATED_BODY()

public:
    AZoneDangerArea();

    void SetDangerRadius(float Radius);

protected:
    virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
    virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, Category = "Danger")
    TObjectPtr<UCapsuleComponent> DangerTrigger;

    UPROPERTY(VisibleAnywhere, Category = "Danger")
    TObjectPtr<UDecalComponent> GroundDecal;

    UPROPERTY(EditDefaultsOnly, Category = "Danger")
    float DamagePerPulse = 10.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Danger",
        meta = (ClampMin = "0.1"))
    float DamageInterval = 1.0f;

private:
    void ApplyDamagePulse();
    void StopDamageTimer();

    FTimerHandle DamageTimerHandle;
    TWeakObjectPtr<AZonePlayerCharacter> PlayerInZone;
};
