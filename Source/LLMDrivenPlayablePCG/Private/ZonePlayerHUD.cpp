// Fill out your copyright notice in the Description page of Project Settings.


#include "ZonePlayerHUD.h"

#include "ZonePlayerCharacter.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UZonePlayerHUD::NativeConstruct()
{
    Super::NativeConstruct();

    // 처음 표시할 때 즉시 갱신
    RefreshHealthDisplay();

    // 이후 0.1초마다 체력 확인
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            HealthRefreshTimerHandle,
            this,
            &UZonePlayerHUD::RefreshHealthDisplay,
            0.1f,
            true
        );
    }
}

void UZonePlayerHUD::NativeDestruct()
{
    // 위젯이 제거되면 갱신 타이머 정리
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(
            HealthRefreshTimerHandle
        );
    }

    Super::NativeDestruct();
}

void UZonePlayerHUD::RefreshHealthDisplay()
{
    AZonePlayerCharacter* Player =
        Cast<AZonePlayerCharacter>(GetOwningPlayerPawn());

    // 플레이어가 아직 준비되지 않았다면 임시 표시
    if (!IsValid(Player))
    {
        if (PB_Health)
        {
            PB_Health->SetPercent(0.0f);
        }

        if (TXT_Health)
        {
            TXT_Health->SetText(FText::FromString(TEXT("-- / --")));
        }

        return;
    }

    if (PB_Health)
    {
        PB_Health->SetPercent(Player->GetHealthPercent());
    }

    if (TXT_Health)
    {
        TXT_Health->SetText(
            FText::Format(
                NSLOCTEXT("ZoneHUD", "HealthDisplay", "{0} / {1}"),
                FText::AsNumber(
                    FMath::RoundToInt(Player->GetCurrentHealth())
                ),
                FText::AsNumber(
                    FMath::RoundToInt(Player->GetMaxHealth())
                )
            )
        );
    }
}

