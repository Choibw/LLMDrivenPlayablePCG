// Fill out your copyright notice in the Description page of Project Settings.


#include "ZonePlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

#include "ZonePlayerHUD.h"

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

void AZonePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IsValid(PlayerHUD))
    {
        PlayerHUD->RemoveFromParent();
        PlayerHUD = nullptr;
    }

    Super::EndPlay(EndPlayReason);
}
