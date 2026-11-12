// Fill out your copyright notice in the Description page of Project Settings.


#include "ZoneDialogueWidget.h"

#include "ZoneDialogueNPC.h"
#include "Components/TextBlock.h"
#include "Components/EditableTextBox.h"

#include "ZonePlayerController.h"
#include "Components/Button.h"

void UZoneDialogueWidget::OpenDialogue(AZoneDialogueNPC* NPC)
{
    if (!IsValid(NPC) || NPC->GetQuestionCount() == 0)
    {
        return;
    }

    DialogueNPC = NPC;
    CurrentQuestionIndex = 0;

    Answers.Reset();
    bDialogueCompleted = false;

    if (TXT_Question)
    {
        TXT_Question->SetText(
            FText::FromString(
                DialogueNPC->GetQuestion(CurrentQuestionIndex)
            )
        );
    }

    if (INPUT_Answer)
    {
        INPUT_Answer->SetText(FText::GetEmpty());
        INPUT_Answer->SetIsReadOnly(false);
    }

    if (BTN_Submit)
    {
        BTN_Submit->SetIsEnabled(true);
    }
}

void UZoneDialogueWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (BTN_Close)
    {
        BTN_Close->OnClicked.AddUniqueDynamic(
            this,
            &UZoneDialogueWidget::HandleCloseClicked
        );
    }

    if (BTN_Submit)
    {
        BTN_Submit->OnClicked.AddUniqueDynamic(
            this,
            &UZoneDialogueWidget::HandleSubmitClicked
        );
    }
}

void UZoneDialogueWidget::FocusAnswerInput()
{
    if (INPUT_Answer && GetOwningPlayer())
    {
        INPUT_Answer->SetUserFocus(GetOwningPlayer());
    }
}

void UZoneDialogueWidget::HandleCloseClicked()
{
    AZonePlayerController* DialogueController =
        Cast<AZonePlayerController>(GetOwningPlayer());

    if (IsValid(DialogueController))
    {
        DialogueController->EndNPCDialogue();
    }
}

void UZoneDialogueWidget::HandleSubmitClicked()
{
    if (bDialogueCompleted
        || !IsValid(DialogueNPC)
        || !INPUT_Answer)
    {
        return;
    }

    if (CurrentQuestionIndex >= DialogueNPC->GetQuestionCount())
    {
        return;
    }

    // 앞뒤 공백을 제거한 답변
    const FString Answer =
        INPUT_Answer->GetText().ToString().TrimStartAndEnd();

    // 빈 답변은 제출하지 않음
    if (Answer.IsEmpty())
    {
        FocusAnswerInput();
        return;
    }

    Answers.Add(Answer);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Dialogue] Answer saved. Index=%d Question=%s Answer=%s"),
        CurrentQuestionIndex,
        *DialogueNPC->GetQuestion(CurrentQuestionIndex),
        *Answer
    );

    ++CurrentQuestionIndex;

    // 모든 질문에 답변한 경우
    if (CurrentQuestionIndex >= DialogueNPC->GetQuestionCount())
    {
        bDialogueCompleted = true;

        if (BTN_Submit)
        {
            BTN_Submit->SetIsEnabled(false);
        }

        INPUT_Answer->SetIsReadOnly(true);

        if (TXT_Question)
        {
            TXT_Question->SetText(
                FText::FromString(TEXT("Dialogue complete."))
            );
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[Dialogue] All answers collected. Count=%d"),
            Answers.Num()
        );

        return;
    }

    // 다음 질문 표시
    if (TXT_Question)
    {
        TXT_Question->SetText(
            FText::FromString(
                DialogueNPC->GetQuestion(CurrentQuestionIndex)
            )
        );
    }

    INPUT_Answer->SetText(FText::GetEmpty());

    FocusAnswerInput();
}
