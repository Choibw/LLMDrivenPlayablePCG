// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZoneDialogueNPC.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class APawn;
class UCameraComponent;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneDialogueNPC : public AActor
{
    GENERATED_BODY()

public:
    AZoneDialogueNPC();

    // NPC가 가지고 있는 질문 개수
    int32 GetQuestionCount() const
    {
        return Questions.Num();
    }

    // 지정한 순서의 질문 반환
    FString GetQuestion(int32 QuestionIndex) const
    {
        if (!Questions.IsValidIndex(QuestionIndex))
        {
            return FString();
        }

        return Questions[QuestionIndex];
    }

    // 플레이어가 대화 가능한 거리인지 확인
    bool CanInteract(const APawn* PlayerPawn) const;

    FTransform GetDialogueStandTransform() const;

    bool HasDialogueCamera() const;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    TObjectPtr<USkeletalMeshComponent> VisualMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dialogue")
    TObjectPtr<USceneComponent> DialogueStandPoint;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dialogue")
    TObjectPtr<UCameraComponent> DialogueCamera;

    UPROPERTY(EditAnywhere, Category = "Dialogue")
    TArray<FString> Questions;

    UPROPERTY(EditAnywhere, Category = "Dialogue",
        meta = (ClampMin = "0.0"))
    float InteractionRadius = 300.0f;
};
