// Fill out your copyright notice in the Description page of Project Settings.

#include "ZoneDialogueNPC.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"

AZoneDialogueNPC::AZoneDialogueNPC()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(
        TEXT("SceneRoot")
    );
    SetRootComponent(SceneRoot);

    VisualMesh = CreateDefaultSubobject<USkeletalMeshComponent>(
        TEXT("VisualMesh")
    );
    VisualMesh->SetupAttachment(SceneRoot);

    VisualMesh->SetSimulatePhysics(false);
    VisualMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));

    DialogueStandPoint = CreateDefaultSubobject<USceneComponent>(
        TEXT("DialogueStandPoint")
    );
    DialogueStandPoint->SetupAttachment(SceneRoot);

    DialogueCamera = CreateDefaultSubobject<UCameraComponent>(
        TEXT("DialogueCamera")
    );
    DialogueCamera->SetupAttachment(SceneRoot);

    DialogueCamera->bUsePawnControlRotation = false;
    DialogueCamera->SetFieldOfView(60.0f);
}

bool AZoneDialogueNPC::CanInteract(const APawn* PlayerPawn) const
{
    if (!IsValid(PlayerPawn))
    {
        return false;
    }

    const float SafeRadius =
        FMath::Max(InteractionRadius, 0.0f);

    const float DistanceSquared = FVector::DistSquared(
        GetActorLocation(),
        PlayerPawn->GetActorLocation()
    );

    return DistanceSquared <= FMath::Square(SafeRadius);
}

FTransform AZoneDialogueNPC::GetDialogueStandTransform() const
{
    return DialogueStandPoint->GetComponentTransform();
}

bool AZoneDialogueNPC::HasDialogueCamera() const
{
    return IsValid(DialogueCamera) && DialogueCamera->IsActive();
}
