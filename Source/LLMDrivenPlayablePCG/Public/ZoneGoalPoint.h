// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ZoneGoalPoint.generated.h"

class USphereComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnZoneGoalCleared, AActor*, GoalActor);

UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZoneGoalPoint : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AZoneGoalPoint();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Goal")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Goal")
    TObjectPtr<UStaticMeshComponent> VisualMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Goal")
    TObjectPtr<USphereComponent> GoalTrigger;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
    bool bStartActive = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Goal")
    bool bDeactivateAfterClear = true;

    UPROPERTY(BlueprintAssignable, Category = "Goal")
    FOnZoneGoalCleared OnZoneGoalCleared;

public:
    UFUNCTION(BlueprintCallable, Category = "Goal")
    void ActivateGoal();

    UFUNCTION(BlueprintCallable, Category = "Goal")
    void DeactivateGoal();

    UFUNCTION(BlueprintCallable, Category = "Goal")
    void SetGoalActive(bool bNewActive);

    UFUNCTION(BlueprintPure, Category = "Goal")
    bool IsGoalActive() const { return bIsGoalActive; }

private:
    UPROPERTY(VisibleAnywhere, Category = "Goal")
    bool bIsGoalActive = false;

private:
    UFUNCTION()
    void OnGoalTriggerBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult
    );

};
