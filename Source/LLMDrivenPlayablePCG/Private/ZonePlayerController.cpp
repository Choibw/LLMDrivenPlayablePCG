// Fill out your copyright notice in the Description page of Project Settings.


#include "ZonePlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

#include "ZonePlayerHUD.h"

#include "ZoneDialogueNPC.h"
#include "ZonePlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "TimerManager.h"

#include "ZoneDialogueWidget.h"

void AZonePlayerController::BeginPlay()
{
	Super::BeginPlay();

	SetShowMouseCursor(false);

	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);

	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, MappingPriority);
			}
		}
	}

    // 이 컴퓨터의 플레이어 화면에만 HUD 생성
    if (IsLocalController())
    {
        if (!PlayerHUDClass)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[PlayerHUD] PlayerHUDClass is not set.")
            );
        }
        else if (!IsValid(PlayerHUD))
        {
            PlayerHUD = CreateWidget<UZonePlayerHUD>(
                this,
                PlayerHUDClass
            );

            if (IsValid(PlayerHUD))
            {
                // 표시용 HUD가 마우스 입력을 가로채지 않도록 설정
                PlayerHUD->SetVisibility(
                    ESlateVisibility::HitTestInvisible
                );

                PlayerHUD->AddToViewport();
            }
        }
    }
}

bool AZonePlayerController::BeginNPCDialogue(AZoneDialogueNPC* NPC)
{
    if (bInDialogue || !IsValid(NPC)
        || !IsValid(PlayerCameraManager))
    {
        return false;
    }

    AZonePlayerCharacter* PlayerCharacter =
        Cast<AZonePlayerCharacter>(GetPawn());

    if (!IsValid(PlayerCharacter)
        || PlayerCharacter->IsDead()
        || !NPC->CanInteract(PlayerCharacter)
        || NPC->GetQuestionCount() == 0
        || !NPC->HasDialogueCamera())
    {
        return false;
    }

    if (!DialogueWidgetClass)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Dialogue] DialogueWidgetClass is not set.")
        );

        return false;
    }

    if (!IsValid(DialogueWidget))
    {
        DialogueWidget = CreateWidget<UZoneDialogueWidget>(
            this,
            DialogueWidgetClass
        );
    }

    if (!IsValid(DialogueWidget))
    {
        return false;
    }

    bInDialogue = true;
    PendingDialogueNPC = NPC;

    PlayerCharacter->StopJumping();
    PlayerCharacter->GetCharacterMovement()
        ->StopMovementImmediately();

    PlayerCharacter->DisableInput(this);
    SetIgnoreLookInput(true);

    // 화면을 검게 만들고, 완료 후에도 검은 상태 유지
    PlayerCameraManager->StartCameraFade(
        0.0f,
        1.0f,
        0.2f,
        FLinearColor::Black,
        false,
        true
    );

    // 완전히 어두워진 뒤 위치와 카메라 변경
    GetWorldTimerManager().SetTimer(
        DialogueEntryTimerHandle,
        this,
        &AZonePlayerController::FinishDialogueEntry,
        0.22f,
        false
    );

    return true;
}

void AZonePlayerController::EndNPCDialogue()
{
    if (!bInDialogue)
    {
        return;
    }

    GetWorldTimerManager().ClearTimer(
        DialogueEntryTimerHandle
    );

    PendingDialogueNPC = nullptr;
    bInDialogue = false;

    if (IsValid(DialogueWidget))
    {
        DialogueWidget->RemoveFromParent();
    }

    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;

    if (IsValid(PlayerCameraManager))
    {
        PlayerCameraManager->StopCameraFade();
    }

    if (AZonePlayerCharacter* PlayerCharacter =
        Cast<AZonePlayerCharacter>(GetPawn()))
    {
        SetViewTargetWithBlend(PlayerCharacter, 0.5f);

        // 사망으로 차단된 입력을 다시 켜지 않음
        if (!PlayerCharacter->IsDead())
        {
            PlayerCharacter->EnableInput(this);
        }
    }

    SetIgnoreLookInput(false);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Dialogue] Ended.")
    );
}

void AZonePlayerController::FinishDialogueEntry()
{
    if (!bInDialogue)
    {
        return;
    }

    AZonePlayerCharacter* PlayerCharacter =
        Cast<AZonePlayerCharacter>(GetPawn());

    if (!IsValid(PlayerCharacter)
        || PlayerCharacter->IsDead()
        || !IsValid(PendingDialogueNPC)
        || !PendingDialogueNPC->HasDialogueCamera()
        || !IsValid(PlayerCameraManager))
    {
        EndNPCDialogue();
        return;
    }

    const FTransform StandTransform =
        PendingDialogueNPC->GetDialogueStandTransform();

    const FRotator StandRotation(
        0.0f,
        StandTransform.Rotator().Yaw,
        0.0f
    );

    const bool bMoved = PlayerCharacter->TeleportTo(
        StandTransform.GetLocation(),
        StandRotation,
        false,
        false
    );

    if (!bMoved)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[Dialogue] Failed to move player to dialogue position.")
        );

        EndNPCDialogue();
        return;
    }

    SetControlRotation(StandRotation);

    // 검은 화면에서 대화 카메라로 즉시 변경
    SetViewTargetWithBlend(
        PendingDialogueNPC.Get(),
        0.0f
    );

    DialogueWidget->OpenDialogue(PendingDialogueNPC.Get());

    if (!DialogueWidget->IsInViewport())
    {
        DialogueWidget->AddToViewport(10);
    }

    bShowMouseCursor = true;

    // 키보드와 마우스 입력을 UI로 전달
    FInputModeUIOnly DialogueInputMode;

    DialogueInputMode.SetLockMouseToViewportBehavior(
        EMouseLockMode::DoNotLock
    );

    SetInputMode(DialogueInputMode);

    // 바로 답변을 입력할 수 있도록 입력칸 선택
    DialogueWidget->FocusAnswerInput();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[Dialogue] Started. NPC=%s"),
        *PendingDialogueNPC->GetName()
    );

    PendingDialogueNPC = nullptr;

    // 캐릭터가 바닥에 자리 잡을 동안 검은 화면 유지
    GetWorldTimerManager().SetTimer(
        DialogueEntryTimerHandle,
        this,
        &AZonePlayerController::FadeInDialogue,
        0.2f,
        false
    );
}

void AZonePlayerController::FadeInDialogue()
{
    if (!bInDialogue || !IsValid(PlayerCameraManager))
    {
        return;
    }

    PlayerCameraManager->StartCameraFade(
        1.0f,
        0.0f,
        0.25f,
        FLinearColor::Black,
        false,
        false
    );
}

void AZonePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IsValid(PlayerHUD))
    {
        PlayerHUD->RemoveFromParent();
        PlayerHUD = nullptr;
    }

    GetWorldTimerManager().ClearTimer(DialogueEntryTimerHandle);
    PendingDialogueNPC = nullptr;

    if (IsValid(DialogueWidget))
    {
        DialogueWidget->RemoveFromParent();
        DialogueWidget = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}
