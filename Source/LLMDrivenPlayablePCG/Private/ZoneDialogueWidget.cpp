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

    if (TXT_SubmitLabel)
    {
        TXT_SubmitLabel->SetText(
            FText::FromString(TEXT("Next"))
        );
    }

    if (BTN_Close)
    {
        BTN_Close->SetIsEnabled(true);
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

bool UZoneDialogueWidget::GetCompletedDialogue(
    TArray<FString>& OutQuestions,
    TArray<FString>& OutAnswers
) const
{
    OutQuestions.Reset();
    OutAnswers.Reset();

    if (!bDialogueCompleted
        || !IsValid(DialogueNPC)
        || Answers.IsEmpty()
        || Answers.Num() != DialogueNPC->GetQuestionCount())
    {
        return false;
    }

    for (int32 Index = 0; Index < Answers.Num(); ++Index)
    {
        OutQuestions.Add(DialogueNPC->GetQuestion(Index));
    }

    OutAnswers = Answers;

    return true;
}

void UZoneDialogueWidget::SetGenerationPending(
    bool bPending,
    const FString& StatusMessage
)
{
    if (BTN_Submit)
    {
        BTN_Submit->SetIsEnabled(!bPending);
    }

    // 요청 중에는 대화창을 닫지 못하게 함
    if (BTN_Close)
    {
        BTN_Close->SetIsEnabled(!bPending);
    }

    if (TXT_SubmitLabel)
    {
        TXT_SubmitLabel->SetText(
            FText::FromString(
                bPending ? TEXT("Generating...") : TEXT("Generate")
            )
        );
    }

    if (TXT_Question && !StatusMessage.IsEmpty())
    {
        TXT_Question->SetText(
            FText::FromString(StatusMessage)
        );
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
    if (!IsValid(DialogueNPC) || !INPUT_Answer)
    {
        return;
    }

    // 답변 수집이 끝났다면 컨트롤러에 생성 요청
    if (bDialogueCompleted)
    {
        AZonePlayerController* DialogueController =
            Cast<AZonePlayerController>(GetOwningPlayer());

        if (IsValid(DialogueController))
        {
            DialogueController->RequestZoneFromDialogue();
        }

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

        if (TXT_SubmitLabel)
        {
            TXT_SubmitLabel->SetText(
                FText::FromString(TEXT("Generate"))
            );
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
