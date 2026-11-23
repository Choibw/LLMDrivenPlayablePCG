// Fill out your copyright notice in the Description page of Project Settings.


#include "ZoneGoalPoint.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"

#include "Kismet/GameplayStatics.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerStart.h"

AZoneGoalPoint::AZoneGoalPoint()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
    VisualMesh->SetupAttachment(SceneRoot);
    VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    GoalTrigger = CreateDefaultSubobject<USphereComponent>(TEXT("GoalTrigger"));
    GoalTrigger->SetupAttachment(SceneRoot);
    GoalTrigger->SetSphereRadius(400.0f);
    GoalTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GoalTrigger->SetCollisionObjectType(ECC_WorldDynamic);
    GoalTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    GoalTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void AZoneGoalPoint::BeginPlay()
{
    Super::BeginPlay();

    GoalTrigger->OnComponentBeginOverlap.AddDynamic(
        this,
        &AZoneGoalPoint::OnGoalTriggerBeginOverlap
    );

    SetGoalActive(bStartActive);
}

void AZoneGoalPoint::ActivateGoal()
{
    SetGoalActive(true);
}

void AZoneGoalPoint::DeactivateGoal()
{
    SetGoalActive(false);
}

void AZoneGoalPoint::SetGoalActive(bool bNewActive)
{
    bIsGoalActive = bNewActive;

    if (GoalTrigger)
    {
        GoalTrigger->SetCollisionEnabled(
            bIsGoalActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision
        );
    }

    UE_LOG(LogTemp, Log, TEXT("[ZoneGoalPoint] Goal Active = %s"),
        bIsGoalActive ? TEXT("true") : TEXT("false"));
}

void AZoneGoalPoint::OnGoalTriggerBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult
)
{
    if (!bIsGoalActive)
    {
        return;
    }

    if (!IsValid(OtherActor) || OtherActor == this)
    {
        return;
    }

    APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
    if (OtherActor != PlayerPawn)
    {
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("[ZoneGoalPoint] Zone Clear!"));

    OnZoneGoalCleared.Broadcast(this);

    if (bDeactivateAfterClear)
    {
        DeactivateGoal();
    }

    TArray<AActor*> LobbyStarts;
    UGameplayStatics::GetAllActorsOfClassWithTag(
        this, APlayerStart::StaticClass(), FName(TEXT("LobbyStart")), LobbyStarts
    );

    if (LobbyStarts.Num() != 1 || !IsValid(LobbyStarts[0]) || !IsValid(PlayerPawn))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ZoneGoalPoint] Expected one LobbyStart."));
        return;
    }

    const AActor* LobbyStart = LobbyStarts[0];
    const FRotator LobbyRotation = LobbyStart->GetActorRotation();

    if (!PlayerPawn->TeleportTo(LobbyStart->GetActorLocation(), LobbyRotation, false, false))
    {
        UE_LOG(LogTemp, Warning, TEXT("[ZoneGoalPoint] Lobby teleport failed."));
        return;
    }

    if (UPawnMovementComponent* Movement = PlayerPawn->GetMovementComponent())
    {
        Movement->StopMovementImmediately();
    }

    if (AController* Controller = PlayerPawn->GetController())
    {
        Controller->SetControlRotation(LobbyRotation);
    }
}
