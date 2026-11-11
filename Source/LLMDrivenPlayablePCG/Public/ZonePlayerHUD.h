// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ZonePlayerHUD.generated.h"

class UProgressBar;
class UTextBlock;

UCLASS()
class LLMDRIVENPLAYABLEPCG_API UZonePlayerHUD : public UUserWidget
{
    GENERATED_BODY()

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UProgressBar> PB_Health;

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<UTextBlock> TXT_Health;

private:
    void RefreshHealthDisplay();

    FTimerHandle HealthRefreshTimerHandle;
};