// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ZoneDialogueWidget.generated.h"

class AZoneDialogueNPC;
class UTextBlock;
class UEditableTextBox;
class UButton;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API UZoneDialogueWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void OpenDialogue(AZoneDialogueNPC* NPC);

    void FocusAnswerInput();

protected:
    virtual void NativeConstruct() override;

protected:
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TXT_Question;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UEditableTextBox> INPUT_Answer;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> BTN_Submit;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UButton> BTN_Close;

private:
    UFUNCTION()
    void HandleCloseClicked();

    UFUNCTION()
    void HandleSubmitClicked();

private:
    UPROPERTY(Transient)
    TObjectPtr<AZoneDialogueNPC> DialogueNPC;

    int32 CurrentQuestionIndex = 0;

    TArray<FString> Answers;

    bool bDialogueCompleted = false;
};
