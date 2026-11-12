// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ZonePlayerController.generated.h"

class UInputMappingContext;
class UZonePlayerHUD;
class AZoneDialogueNPC;
class UZoneDialogueWidget;

/**
 * 
 */
UCLASS()
class LLMDRIVENPLAYABLEPCG_API AZonePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	bool BeginNPCDialogue(AZoneDialogueNPC* NPC);

	void EndNPCDialogue();
	
protected:
	virtual void BeginPlay() override;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	int32 MappingPriority = 0;

protected:
	// 화면에 표시할 위젯 BP 클래스
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UZonePlayerHUD> PlayerHUDClass;

	UPROPERTY(EditDefaultsOnly, Category = "Dialogue")
	TSubclassOf<UZoneDialogueWidget> DialogueWidgetClass;

private:
	void FinishDialogueEntry();

	void FadeInDialogue();

private:
	// 실제 생성된 HUD
	UPROPERTY(Transient)
	TObjectPtr<UZonePlayerHUD> PlayerHUD;

private:
	bool bInDialogue = false;

	FTimerHandle DialogueEntryTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AZoneDialogueNPC> PendingDialogueNPC;

	UPROPERTY(Transient)
	TObjectPtr<UZoneDialogueWidget> DialogueWidget;
};
