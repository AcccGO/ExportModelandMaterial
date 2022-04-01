// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GetModelStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FGetModelStyle::StyleInstance = NULL;

void FGetModelStyle::Initialize()
{
    if (!StyleInstance.IsValid()) {
        StyleInstance = Create();
        FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
    }
}

void FGetModelStyle::Shutdown()
{
    FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
    ensure(StyleInstance.IsUnique());
    StyleInstance.Reset();
}

FName FGetModelStyle::GetStyleSetName()
{
    static FName StyleSetName(TEXT("GetModelStyle"));
    return StyleSetName;
}

#define IMAGE_BRUSH(RelativePath, ...)  FSlateImageBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BOX_BRUSH(RelativePath, ...)    FSlateBoxBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define BORDER_BRUSH(RelativePath, ...) FSlateBorderBrush(Style->RootToContentDir(RelativePath, TEXT(".png")), __VA_ARGS__)
#define TTF_FONT(RelativePath, ...)     FSlateFontInfo(Style->RootToContentDir(RelativePath, TEXT(".ttf")), __VA_ARGS__)
#define OTF_FONT(RelativePath, ...)     FSlateFontInfo(Style->RootToContentDir(RelativePath, TEXT(".otf")), __VA_ARGS__)

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef<FSlateStyleSet> FGetModelStyle::Create()
{
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet("GetModelStyle"));
    Style->SetContentRoot(IPluginManager::Get().FindPlugin("GetModel")->GetBaseDir() / TEXT("Resources"));

    Style->Set("GetModel.OpenPluginWindow", new IMAGE_BRUSH(TEXT("ButtonIcon_40x"), Icon40x40));

    return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FGetModelStyle::ReloadTextures()
{
    if (FSlateApplication::IsInitialized()) {
        FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
    }
}

const ISlateStyle& FGetModelStyle::Get()
{
    return *StyleInstance;
}
