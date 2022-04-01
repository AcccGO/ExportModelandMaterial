// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "GetModelStyle.h"

class FGetModelCommands : public TCommands<FGetModelCommands>
{
public:
    FGetModelCommands()
        : TCommands<FGetModelCommands>(TEXT("GetModel"), NSLOCTEXT("Contexts", "GetModel", "GetModel Plugin"), NAME_None, FGetModelStyle::GetStyleSetName())
    {
    }

    // TCommands<> interface
    virtual void RegisterCommands() override;

public:
    TSharedPtr<FUICommandInfo> OpenPluginWindow;
};