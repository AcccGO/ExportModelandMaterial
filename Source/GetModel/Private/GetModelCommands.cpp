// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GetModelCommands.h"

#define LOCTEXT_NAMESPACE "FGetModelModule"

void FGetModelCommands::RegisterCommands()
{
    UI_COMMAND(OpenPluginWindow, "GetModel", "Bring up GetModel window", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE