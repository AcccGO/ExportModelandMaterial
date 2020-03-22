// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.
#include "GetModel.h"
#include "GetModelStyle.h"
#include "GetModelCommands.h"

#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "widgets/Input/SButton.h"
#include "MaterialUtilities/Public/MaterialUtilities.h"
#include "UnrealEd.h"
#include "MeshMergeDataTracker.h"
#include "RawMesh/Public/RawMesh.h"
#include "MaterialBaking/Public/MaterialBakingStructures.h"
#include "Private/ProxyMaterialUtilities.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "StaticMeshComponentAdapter.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "Private/MeshMergeHelpers.h"
#include "MaterialBaking/Public/MaterialOptions.h"
#include "MeshUtilities.h"
#include "UObject/Object.h"
#include "Modules/ModuleManager.h"
#include "MaterialBaking/Public/IMaterialBakingModule.h"
#include "MaterialBaking/Private/MaterialBakingModule.h"
#include "UObjectGlobals.h"
#include "Engine/MeshMerging.h"
#include "Misc/PackageName.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "SkeletalRenderPublic.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Package.h"
#include "Misc/ScopedSlowTask.h"
#include "IHierarchicalLODUtilities.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "ImageUtils.h"
#include "Developer/MeshMergeUtilities/Private/MeshMergeEditorExtensions.h"
#include "Misc/FileHelper.h"
#include "Async/ParallelFor.h"
#include "AssetExportTask.h"
#include "Array.h"
#include "MeshMergeUtilities/Private/MeshMergeUtilities.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Components/SkinnedMeshComponent.h"
#include "SkeletalMeshTypes.h"
#include "Materials/Material.h"
#include "MeshMergeData.h"
#include "Engine/MeshMergeCullingVolume.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "Private/ProxyGenerationProcessor.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "PhysicsEngine/ConvexElem.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"
#include "IMaterialBakingAdapter.h"
#include "SkeletalMeshAdapter.h"
#include "StaticMeshAdapter.h"
#include "Settings/EditorExperimentalSettings.h"
#include "ScopedTransaction.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/LODActor.h"
#include "HierarchicalLODVolume.h"
#include "Engine/Selection.h"
#include "MaterialBaking/Public/MaterialBakingHelpers.h"
#include "IMeshMergeExtension.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshAttributeArray.h"
#include "MeshDescriptionOperations.h"
#include "Archive.h"
#include "SCheckBox.h"
#include "ContentBrowser/Public/ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Exporters/Exporter.h"
#include "ObjectTools.h"
#include "Transform.h"
#include "Math/TransformNonVectorized.h"

static const FName FGetModelModuleTabName("GetModel");

#define LOCTEXT_NAMESPACE "FGetModelModule"

TSharedPtr<SSpinBox<int32>> TextureSizeX;
TSharedPtr<SSpinBox<int32>> TextureSizeY;

TSharedPtr<SCheckBox> bUseVertexDataForBakingMaterial;
TSharedPtr<SCheckBox> bMergeMaterials;
TSharedPtr<SCheckBox> bMergePhysicsData;
TSharedPtr<SCheckBox> bNormalMap;
TSharedPtr<SCheckBox> bMRSMap;
TSharedPtr<SCheckBox> bOpacityMap;
TSharedPtr<SCheckBox> bEmissiveMap;
TSharedPtr<SCheckBox> bInstancingMultiActors;
TMap<FString, int32> InstancedMultiActors;

inline FString FixupMaterialName(UMaterialInterface* Material)
{
	return Material->GetName().Replace(TEXT("."), TEXT("_")).Replace(TEXT(":"), TEXT("_"));
}

void FGetModelModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FGetModelStyle::Initialize();
	FGetModelStyle::ReloadTextures();

	FGetModelCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FGetModelCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FGetModelModule::PluginButtonClicked),
		FCanExecuteAction());

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("WindowLayout", EExtensionHook::After, PluginCommands, FMenuExtensionDelegate::CreateRaw(this, &FGetModelModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	{
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension("Settings", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FGetModelModule::AddToolbarExtension));

		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(FGetModelModuleTabName, FOnSpawnTab::CreateRaw(this, &FGetModelModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FFGetModelModuleTabTitle", "Get Model and Material"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);
}

void FGetModelModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FGetModelStyle::Shutdown();

	FGetModelCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(FGetModelModuleTabName);
}

FReply FGetModelModule::GenerateMergeObj()
{
	GetObjandMaterialMethod(false);
	return FReply::Handled();
}

FReply FGetModelModule::ExportMergeObj()
{
	GetObjandMaterialMethod(true);
	return FReply::Handled();
}

void FGetModelModule::GetObjandMaterialMethod(bool bExport)
{
	TArray<AActor*> Actors;
	TArray<UPrimitiveComponent*> Components;

	GWarn->BeginSlowTask(NSLOCTEXT("UnrealEd", "ExportingOBJandMaterial", "Exporting Material and OBJ"), true);

	{
		USelection* SelectedActors = GEditor->GetSelectedActors();
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AActor* Actor = Cast<AActor>(*Iter);
			if (Actor)
			{
				Actors.Add(Actor);
			}
		}

		{
			// Retrieve static mesh components from selected actors
			//SelectedComponents.Empty();
			for (int32 ActorIndex = 0; ActorIndex < Actors.Num(); ++ActorIndex)
			{
				AActor* Actor = Actors[ActorIndex];
				check(Actor != nullptr);

				TArray<UChildActorComponent*> ChildActorComponents;
				Actor->GetComponents<UChildActorComponent>(ChildActorComponents);
				for (UChildActorComponent* ChildComponent : ChildActorComponents)
				{
					// Push actor at the back of array so we will process it
					AActor* ChildActor = ChildComponent->GetChildActor();
					if (ChildActor)
					{
						Actors.Add(ChildActor);
					}
				}

				TArray<UPrimitiveComponent*> PrimComponents;
				Actor->GetComponents<UPrimitiveComponent>(PrimComponents);
				for (UPrimitiveComponent* PrimComponent : PrimComponents)
				{
					UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(PrimComponent);
					if (MeshComponent &&
						MeshComponent->GetStaticMesh() != nullptr &&
						MeshComponent->GetStaticMesh()->GetSourceModels().Num() > 0)
					{
						Components.Add(MeshComponent);
					}

					UShapeComponent* ShapeComponent = Cast<UShapeComponent>(PrimComponent);
					if (ShapeComponent)
					{
						Components.Add(ShapeComponent);
					}
				}
			}
		}
	}

	for (int32 Index = 0; Index < Components.Num(); Index++)
	{
		GWarn->StatusUpdate(Index, Components.Num(), NSLOCTEXT("UnrealEd", "ExportingOBJandMaterial", "Exporting Material and OBJ"));

		UPrimitiveComponent* Component = Components[Index];
		FMeshMergingSettings settings;
		settings.bUseVertexDataForBakingMaterial = bUseVertexDataForBakingMaterial->IsChecked();
		settings.LODSelectionType = EMeshLODSelectionType::SpecificLOD;
		settings.bMergeMaterials = true/*bMergeMaterials->IsChecked()*/;
		settings.bMergePhysicsData = bMergePhysicsData->IsChecked();
		settings.MaterialSettings.bNormalMap = bNormalMap->IsChecked();
		settings.MaterialSettings.bMetallicMap = bMRSMap->IsChecked();
		settings.MaterialSettings.bSpecularMap = bMRSMap->IsChecked();
		settings.MaterialSettings.bRoughnessMap = bMRSMap->IsChecked();
		settings.MaterialSettings.bOpacityMap = bOpacityMap->IsChecked();
		settings.MaterialSettings.bOpacityMaskMap = bOpacityMap->IsChecked();
		settings.MaterialSettings.bEmissiveMap = bEmissiveMap->IsChecked();
		settings.bIncludeImposters = true;
		settings.MaterialSettings.BlendMode = BLEND_Masked;
		settings.MaterialSettings.TextureSize = FIntPoint(TextureSizeX->GetValue(), TextureSizeY->GetValue());
		settings.bPivotPointAtZero = false;

		TArray<UObject*> AssetsToSync;
		FVector MergedActorLocation;
		const IMeshMergeUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities").GetUtilities();

		// Merge...
		{
			//path
			FString ProjectPath = "/Game/GetObjandMaterial/materials/";
			FString ComponentName = FPackageName::GetShortName(Cast<UStaticMeshComponent>(Component)->GetStaticMesh()->GetOutermost()->GetName());

			FText T = FText::FromString(TEXT("Merging ") + ComponentName + TEXT("..."));
			FScopedSlowTask SlowTask(0, T);
			SlowTask.MakeDialog();

			UWorld* World = Component->GetWorld();
			checkf(World != nullptr, TEXT("Invalid World retrieved from Mesh components"));
			const float ScreenAreaSize = TNumericLimits<float>::Max();

			//component to merge
			TArray<UPrimitiveComponent*> ComponentsToMerge;
			ComponentsToMerge.Add(Component);

			//get lod
			UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component);
			auto ModelTransfom = StaticMeshComponent->GetRelativeTransform();

			int32 LOD_index;
			for (LOD_index = 0; LOD_index < StaticMeshComponent->GetStaticMesh()->GetNumLODs(); ++LOD_index)
			{
				//merge mesh and material
				settings.SpecificLOD = LOD_index;
				MeshUtilities.MergeComponentsToStaticMesh(ComponentsToMerge, World, settings, nullptr, nullptr, ProjectPath + ComponentName + TEXT("_LOD") + FString::FromInt(LOD_index), AssetsToSync, MergedActorLocation, ScreenAreaSize, false);

				//save
				if (AssetsToSync.Num())
				{
					//save in project
					FAssetRegistryModule& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
					int32 AssetCount = AssetsToSync.Num();
					for (int32 AssetIndex = 0; AssetIndex < AssetCount; AssetIndex++)
					{
						AssetRegistry.AssetCreated(AssetsToSync[AssetIndex]);
						GEditor->BroadcastObjectReimported(AssetsToSync[AssetIndex]);
					}
					//Also notify the content browser that the new assets exists
					FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
					ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync, true);

					if (bExport)
					{
						//export maps
						TMap<FString, FString>MTLs = ExportMaterialToBMP(AssetsToSync);

						//instance multi actors
						FString SavePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()) + TEXT("GetObjandMaterial/");
						FString ObjPath = SavePath + ComponentName + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".obj");
						FString MtlPath = SavePath + ComponentName + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".mtl");
						if (bInstancingMultiActors->IsChecked())
						{
							if (auto it = InstancedMultiActors.Find(ComponentName + TEXT("_LOD") + FString::FromInt(LOD_index)))
							{
								*it += 1;
								ObjPath = SavePath + ComponentName + TEXT("_ACTOR") + FString::FromInt(*it) + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".obj");
								MtlPath = SavePath + ComponentName + TEXT("_ACTOR") + FString::FromInt(*it) + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".mtl");
							}
							else
							{
								InstancedMultiActors.Add(ComponentName + TEXT("_LOD") + FString::FromInt(LOD_index), 0);
								ObjPath = SavePath + ComponentName + TEXT("_ACTOR0") + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".obj");
								MtlPath = SavePath + ComponentName + TEXT("_ACTOR0") + TEXT("_LOD") + FString::FromInt(LOD_index) + TEXT(".mtl");
							}
						}

						//export obj
						UStaticMesh* MergedMesh = nullptr;
						TArray<FString>MtlNames;
						if (AssetsToSync.FindItemByClass(&MergedMesh))
						{
							MtlNames = ExportObj(MergedMesh, ObjPath, LOD_index, StaticMeshComponent);
						}

						//export mtl
						//UMaterialInstanceConstant* MergedMaterial = nullptr;
						//if (AssetsToSync.FindItemByClass(&MergedMaterial))
						{
							FOutputDeviceFile* MaterialFile = new FOutputDeviceFile(*MtlPath, true);
							MaterialFile->SetSuppressEventTag(true);
							MaterialFile->SetAutoEmitLineTerminator(false);

							MaterialFile->Logf(TEXT("newmtl %s\r\n"), *MtlNames[0]);
							for (auto mtlItor = MTLs.CreateIterator(); mtlItor; ++mtlItor)
							{
								MaterialFile->Logf(TEXT("\t%s %s\r\n"), *mtlItor.Key(), *mtlItor.Value());
							}
							MaterialFile->Logf(TEXT("\r\n\n"));
							MaterialFile->TearDown();
							delete MaterialFile;
						}
					}
				}
			}
		}
	}

	Actors.Empty();
	Components.Empty();

	InstancedMultiActors.Empty();

	GWarn->EndSlowTask();
	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Export Material and OBJ DONE")));
}

TArray<FString> FGetModelModule::ExportObj(UStaticMesh* MergedMesh, FString& ObjPath, int LOD_index, UStaticMeshComponent* StaticMeshComponent)
{
	TArray<FString>MtlNames;

	FArchive* ObjFile = IFileManager::Get().CreateFileWriter(*ObjPath);
	ObjFile->Logf(TEXT("# UnrealEd OBJ exporter\r\n"));

	TArray<FStaticMaterial> StaticMaterials = MergedMesh->StaticMaterials;
	const FStaticMeshLODResources& RenderData = MergedMesh->GetLODForExport(LOD_index);
	uint32 VertexCount = RenderData.GetNumVertices();
	check(VertexCount == RenderData.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices());

	ObjFile->Logf(TEXT("\r\n"));
	ObjFile->Logf(TEXT("mtllib %s\n"), *FPaths::GetCleanFilename(ObjPath));

	for (uint32 i = 0; i < VertexCount; i++)
	{
		const FVector& OSPos = RenderData.VertexBuffers.PositionVertexBuffer.VertexPosition(i);
		//const FVector WSPos = StaticMeshComponent->LocalToWorld.TransformPosition( OSPos );

		FVector Temp = OSPos;
		const FVector WPos = StaticMeshComponent->GetComponentTransform().GetLocation() + Temp;
		//const FVector WPos = StaticMeshComponent->GetComponentToWorld().TransformPosition(Temp);
		//const FVector WPos = StaticMeshComponent->GetRelativeTransform().TransformVector(Temp);

		// Transform to Lightwave's coordinate system
		ObjFile->Logf(TEXT("v %f %f %f\r\n"), WPos.X, WPos.Z, WPos.Y);
	}
	ObjFile->Logf(TEXT("\r\n"));

	for (uint32 i = 0; i < VertexCount; ++i)
	{
		// takes the first UV
		//const FVector2D UV = RenderData.VertexBuffer.GetVertexUV(i, 0);
		const FVector2D UV = RenderData.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(i, 0);

		// Invert the y-coordinate (Lightwave has their bitmaps upside-down from us).
		ObjFile->Logf(TEXT("vt %f %f\r\n"), UV.X, 1.0f - UV.Y);
	}

	ObjFile->Logf(TEXT("\r\n"));

	for (uint32 i = 0; i < VertexCount; ++i)
	{
		//const FVector& OSNormal = RenderData.VertexBuffer.VertexTangentZ(i);
		const FVector& OSNormal = RenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(i);
		FVector Temp = OSNormal;
		//const FVector WNormal = StaticMeshComponent->GetRelativeTransform().TransformVector(Temp);

		// Transform to Lightwave's coordinate system
		ObjFile->Logf(TEXT("vn %f %f %f\r\n"), Temp.X, Temp.Z, Temp.Y);
	}

	TArray<uint32> MaterialIndexArray;
	for (auto temp : RenderData.Sections)
	{
		MaterialIndexArray.Add(temp.FirstIndex);
	}

	{
		FIndexArrayView Indices = RenderData.IndexBuffer.GetArrayView();
		uint32 NumIndices = Indices.Num();

		check(NumIndices % 3 == 0); int count = 0;
		for (uint32 i = 0; i < NumIndices / 3; i++)
		{
			if (count < MaterialIndexArray.Num() && i == MaterialIndexArray[count] / 3)
			{
				FString mtl = StaticMaterials[count].MaterialSlotName.ToString() + TEXT("_") + StaticMaterials[count].MaterialSlotName.ToString();
				ObjFile->Logf(TEXT("usemtl %s\n"), *FPaths::GetCleanFilename(mtl));//*FPaths::GetCleanFilename(MaterialName));
				MtlNames.Add(FPaths::GetCleanFilename(mtl));
				count++;
			}

			// Wavefront indices are 1 based
			uint32 a = Indices[3 * i] + 1;
			uint32 b = Indices[3 * i + 1] + 1;
			uint32 c = Indices[3 * i + 2] + 1;

			ObjFile->Logf(TEXT("f %d/%d/%d %d/%d/%d %d/%d/%d\r\n"),
				a, a, a,
				b, b, b,
				c, c, c);
		}
	}

	ObjFile->Logf(TEXT("# UnrealEd OBJ exporter\r\n"));
	delete ObjFile;

	return MtlNames;
}

TMap<FString, FString> FGetModelModule::ExportMaterialToBMP(TArray<UObject*>& ObjectsToExport)
{
	TMap<FString, FString> mtls;
	TArray<UExporter*> Exporters;

	auto TransientPackage = GetTransientPackage();
	// @todo DB: Assemble this set once.
	Exporters.Empty();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UExporter::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			UExporter* Exporter = NewObject<UExporter>(TransientPackage, *It);
			Exporters.Add(Exporter);
		}
	}
	//AssembleListOfExporters(Exporters);

	//BMP 变成了[21]而非20
	UExporter* ExporterUse = Exporters[21];

	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	for (int32 Index = 0; Index < ObjectsToExport.Num(); Index++)
	{
		FString MapType;

		UObject* ObjectToExport = ObjectsToExport[Index];
		const bool bObjectIsSupported = ExporterUse->SupportsObject(ObjectToExport);

		if (!ObjectToExport || !bObjectIsSupported)
		{
			continue;
		}

		FString name = ObjectToExport->GetName().Replace(TEXT("_LOD0"), TEXT(""));

		name = name.Replace(TEXT(" - "), TEXT("_")).Replace(TEXT("-"), TEXT("_"));


		FString Filename = TEXT("maps/") + name;

		TArray<FString> ParsedName;
		ObjectToExport->GetName().ParseIntoArray(ParsedName, TEXT("_"), true);
		auto NameType = ParsedName.Last();
		if (NameType == TEXT("Diffuse"))
		{
			MapType = TEXT("map_Kd ");
		}
		else if (NameType == TEXT("MRS"))
		{
			mtls.FindOrAdd(TEXT("map_Ks ")) = Filename + TEXT(".bmp");
			mtls.FindOrAdd(TEXT("map_Pr ")) = Filename + TEXT(".bmp");
			MapType = TEXT("map_Pm ");
		}
		else if (NameType == TEXT("Normal"))
		{
			MapType = TEXT("norm ");
		}
		else if (NameType == TEXT("Opacity"))
		{
			MapType = TEXT("map_Tr ");
		}
		else if (NameType == TEXT("OpacityMask"))
		{
			MapType = TEXT("map_TrMask ");
		}
		else if (NameType == TEXT("Emissive"))
		{
			MapType = TEXT("map_Ke ");
		}

		Filename += TEXT(".bmp");
		auto&T = mtls.FindOrAdd(MapType);
		T = Filename;

		Filename = ProjectPath + TEXT("GetObjandMaterial/") + Filename;

		UExporter::ExportToFile(ObjectToExport, ExporterUse, *Filename, false, false, false);
	}

	return mtls;
}

TSharedRef<SDockTab> FGetModelModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedPtr<SButton> GetMaterialBtn;
	TSharedPtr<SButton> GetObjBtn;

	TSharedRef<SDockTab> mainTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.ShouldAutosize(true)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("TextureSize(X,Y):")))
		]
	+ SHorizontalBox::Slot().HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			SAssignNew(TextureSizeX, SSpinBox<int32>)
			//SNew(SSpinBox<int32>)
		.MaxValue(16384)
		.MinValue(1)
		.Value(1024)
		]
	+ SHorizontalBox::Slot().HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			SAssignNew(TextureSizeY, SSpinBox<int32>)
			.Value(1024)
		.MaxValue(16384)
		.MinValue(1)
		]
		]
	//bInstancingMultiActors
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bInstancingMultiActors, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Instancing Multi Actors"))).IsChecked(ECheckBoxState::Checked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Instancing Multi Actors")))
		]
		]
	//checkbox Use Vertex Data For Baking Material
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bUseVertexDataForBakingMaterial, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Use Vertex Data For Baking Material")))
		.IsChecked(ECheckBoxState::Checked)
		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Use Vertex Data For Baking Material")))
		]
		]
	//checkbox Merge Physics Data
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bMergePhysicsData, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Merge Physics Data"))).IsChecked(ECheckBoxState::Unchecked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Merge Physics Data")))
		]
		]
	// //checkbox merge materials
	// + SScrollBox::Slot().Padding(10, 5)
	// 	[
	// 		SNew(SHorizontalBox)
	// 		+ SHorizontalBox::Slot()
	// 	//.AutoWeight()
	// 	.HAlign(HAlign_Center)
	// 	.Padding(4, 4, 10, 4)
	// 	.AutoWidth()
	// 	[
	// 		//SNew(SButton)
	// 		SAssignNew(bMergeMaterials, SCheckBox)
	// 		.ToolTipText(FText::FromString(TEXT("Merge Materials"))).IsChecked(ECheckBoxState::Checked)

	// 	]
	// + SHorizontalBox::Slot()
	// 	.AutoWidth()
	// 	.HAlign(HAlign_Left)
	// 	.Padding(4, 4, 10, 4)
	// 	[
	// 		SNew(STextBlock)
	// 		.Text(FText::FromString(TEXT("Merge Materials")))
	// 	]
	// 	]
	//checkbox n
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bNormalMap, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Export Normal Map"))).IsChecked(ECheckBoxState::Checked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Export Normal Map")))
		]
		]
	//checkbox mrs
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bMRSMap, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Export MRS Map"))).IsChecked(ECheckBoxState::Checked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Export MRS Map")))
		]
		]
	//checkbox o
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bOpacityMap, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Export Opacity Map"))).IsChecked(ECheckBoxState::Checked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Export Opacity Map")))
		]
		]
	//checkbox e
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(bEmissiveMap, SCheckBox)
			.ToolTipText(FText::FromString(TEXT("Export Emissive Map"))).IsChecked(ECheckBoxState::Unchecked)

		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(4, 4, 10, 4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Export Emissive Map")))
		]
		]
	//button
	+ SScrollBox::Slot().Padding(10, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		//.AutoWeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			//SNew(SButton)
			SAssignNew(GetMaterialBtn, SButton)
			.Text(LOCTEXT("GetMaterial", "Get material")).OnClicked_Raw(this, &FGetModelModule::GenerateMergeObj)
		]
	+ SHorizontalBox::Slot()
		//.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(4, 4, 10, 4)
		.AutoWidth()
		[
			SAssignNew(GetObjBtn, SButton)
			.Text(LOCTEXT("GetObjAndMtl", "Export obj And mtl")).OnClicked_Raw(this, &FGetModelModule::ExportMergeObj)
		]
		]
		];
	return mainTab;
}

void FGetModelModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->InvokeTab(FGetModelModuleTabName);
}

void FGetModelModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.AddMenuEntry(FGetModelCommands::Get().OpenPluginWindow);
}

void FGetModelModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddToolBarButton(FGetModelCommands::Get().OpenPluginWindow);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGetModelModule, GetModel)