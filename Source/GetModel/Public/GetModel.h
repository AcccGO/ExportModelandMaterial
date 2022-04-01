// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FToolBarBuilder;
class FMenuBuilder;

class FGetModelModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** This function will be bound to Command (by default it will bring up plugin window) */
    void PluginButtonClicked();

    FReply                 GenerateMergeObj();
    FReply                 ExportMergeObj();
    void                   GetObjandMaterialMethod(bool bExport);
    TMap<FString, FString> ExportMaterialToBMP(TArray<UObject*>& ObjectsToExport);
    TArray<FString>        ExportObj(UStaticMesh* MergedMesh, FString& ObjPath, int LOD_index, UStaticMeshComponent* StaticMeshComponent);

private:
    void AddToolbarExtension(FToolBarBuilder& Builder);
    void AddMenuExtension(FMenuBuilder& Builder);

    TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
    TSharedPtr<class FUICommandList> PluginCommands;
};
