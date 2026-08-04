// Fill out your copyright notice in the Description page of Project Settings.


#include "ZonePlayerController.h"

#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

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
}
