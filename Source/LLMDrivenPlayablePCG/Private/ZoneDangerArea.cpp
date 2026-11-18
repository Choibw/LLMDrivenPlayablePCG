// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneDangerArea.h"

#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"

#include "Engine/World.h"
#include "ZonePlayerCharacter.h"

AZoneDangerArea::AZoneDangerArea()
{
    PrimaryActorTick.bCanEverTick = false;

    DangerTrigger = CreateDefaultSubobject<UCapsuleComponent>(
        TEXT("DangerTrigger")
    );
    SetRootComponent(DangerTrigger);

    DangerTrigger->InitCapsuleSize(300.0f, 900.0f);
    DangerTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DangerTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    DangerTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    DangerTrigger->SetGenerateOverlapEvents(true);

    GroundDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("GroundDecal"));
    GroundDecal->SetupAttachment(DangerTrigger);
    GroundDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
    GroundDecal->SetRelativeLocation(FVector(0.0f, 0.0f, -400.0f));
    GroundDecal->DecalSize = FVector(60.0f, 100.0f, 100.0f);
}

void AZoneDangerArea::SetDangerRadius(float Radius)
{
    const float SafeRadius = FMath::Max(Radius, 1.0f);
    DangerTrigger->SetCapsuleSize(SafeRadius, SafeRadius + 600.0f, true);

    const float DecalScale = SafeRadius / 100.0f;
    GroundDecal->SetRelativeScale3D(FVector(1.0f, DecalScale, DecalScale));
}

void AZoneDangerArea::NotifyActorBeginOverlap(AActor* OtherActor)
{
    Super::NotifyActorBeginOverlap(OtherActor);

    AZonePlayerCharacter* ZonePlayer = Cast<AZonePlayerCharacter>(OtherActor);

    if (!GetWorld() || !GetWorld()->IsGameWorld()
        || !IsValid(ZonePlayer) || ZonePlayer->IsDead())
    {
        return;
    }

    PlayerInZone = ZonePlayer;

    GetWorldTimerManager().SetTimer(
        DamageTimerHandle,
        this,
        &AZoneDangerArea::ApplyDamagePulse,
        FMath::Max(DamageInterval, 0.1f),
        true
    );
}

void AZoneDangerArea::NotifyActorEndOverlap(AActor* OtherActor)
{
    Super::NotifyActorEndOverlap(OtherActor);

    if (OtherActor == PlayerInZone.Get())
    {
        StopDamageTimer();
    }
}

void AZoneDangerArea::ApplyDamagePulse()
{
    AZonePlayerCharacter* ZonePlayer = PlayerInZone.Get();

    if (!IsValid(ZonePlayer) || ZonePlayer->IsDead()
        || !DangerTrigger->IsOverlappingActor(ZonePlayer))
    {
        StopDamageTimer();
        return;
    }

    const float Radius = DangerTrigger->GetScaledCapsuleRadius();

    if (FVector::DistSquared2D(
        ZonePlayer->GetActorLocation(),
        DangerTrigger->GetComponentLocation()
    ) > FMath::Square(Radius))
    {
        return;
    }

    ZonePlayer->ApplyDamage(DamagePerPulse);

    if (ZonePlayer->IsDead())
    {
        StopDamageTimer();
    }
}

void AZoneDangerArea::StopDamageTimer()
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(DamageTimerHandle);
    }

    PlayerInZone.Reset();
}

void AZoneDangerArea::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopDamageTimer();
    Super::EndPlay(EndPlayReason);
}

