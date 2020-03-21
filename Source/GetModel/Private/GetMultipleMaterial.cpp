#include "GetMultipleMaterial.h"
#include "Private/ProxyMaterialUtilities.h"
#include "AssetRegistryModule.h"
#include "Editor.h"
#include "MeshMergeDataTracker.h"
#include "StaticMeshComponentAdapter.h"
#include "HierarchicalLODUtilitiesModule.h"
#include "Private/MeshMergeHelpers.h"
#include "MaterialBaking/Public/MaterialOptions.h"
#include "MeshUtilities.h"
#include "Developer/RawMesh/Public/RawMesh.h"
#include "Developer/MaterialUtilities/Public/MaterialUtilities.h"
#include "UObject/Object.h"
#include "Modules/ModuleManager.h"
#include "MaterialBaking/Public/IMaterialBakingModule.h"
#include "MaterialBaking/Private/MaterialBakingModule.h"
#include "UObjectGlobals.h"
#include "Developer/MaterialBaking/Public/MaterialBakingStructures.h"
#include "Engine/MeshMerging.h"
#include "Misc/PackageName.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/ShapeComponent.h"
#include "SkeletalRenderPublic.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Package.h"
#include "Materials/Material.h"
#include "Misc/ScopedSlowTask.h"
#include "IHierarchicalLODUtilities.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "ImageUtils.h"
#include "Developer/MeshMergeUtilities/Private/MeshMergeEditorExtensions.h"
#include "Misc/FileHelper.h"
#include "Async/ParallelFor.h"
//#include "AssetToolsModule.h"
#include "Exporters/Exporter.h"

//#include "Editor/UnrealEd/Private/UnrealEdPrivatePCH.h"
//#include "Private/UnrealEdPrivatePCH.h"
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

GetMultipleMaterial::GetMultipleMaterial()
{
}

GetMultipleMaterial::~GetMultipleMaterial()
{
}

EFlattenMaterialProperties GetMultipleMaterial::NewToOldProperty_new(int32 NewProperty)
{
	const EFlattenMaterialProperties Remap[MP_Refraction] =
	{
		EFlattenMaterialProperties::Emissive,
		EFlattenMaterialProperties::Opacity,
		EFlattenMaterialProperties::OpacityMask,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::Diffuse,
		EFlattenMaterialProperties::Metallic,
		EFlattenMaterialProperties::Specular,
		EFlattenMaterialProperties::Roughness,
		EFlattenMaterialProperties::Normal,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::NumFlattenMaterialProperties,
		EFlattenMaterialProperties::AmbientOcclusion
	};

	return Remap[NewProperty];
}

void GetMultipleMaterial::ConvertOutputToFlatMaterials_new(const TArray<FBakeOutput>& BakeOutputs, const TArray<FMaterialData>& MaterialData, TArray<FFlattenMaterial> &FlattenedMaterials)
{
	for (int32 OutputIndex = 0; OutputIndex < BakeOutputs.Num(); ++OutputIndex)
	{
		const FBakeOutput& Output = BakeOutputs[OutputIndex];
		const FMaterialData& MaterialInfo = MaterialData[OutputIndex];

		FFlattenMaterial Material;

		for (TPair<EMaterialProperty, FIntPoint> SizePair : Output.PropertySizes)
		{
			EFlattenMaterialProperties OldProperty = GetMultipleMaterial::NewToOldProperty_new(SizePair.Key);
			Material.SetPropertySize(OldProperty, SizePair.Value);
			Material.GetPropertySamples(OldProperty).Append(Output.PropertyData[SizePair.Key]);
		}

		Material.bDitheredLODTransition = MaterialInfo.Material->IsDitheredLODTransition();
		Material.BlendMode = BLEND_Opaque;
		Material.bTwoSided = MaterialInfo.Material->IsTwoSided();
		Material.EmissiveScale = Output.EmissiveScale;

		FlattenedMaterials.Add(Material);
	}
}

void GetMultipleMaterial::CreateProxyMaterial_new(const FString &InBasePackageName, FString MergedAssetPackageName, UPackage* InOuter, const FMeshMergingSettings &InSettings, FFlattenMaterial OutMaterial, TArray<UObject *>& OutAssetsToSync)
{
	// Create merged material asset
	FString MaterialAssetName;
	FString MaterialPackageName;
	if (InBasePackageName.IsEmpty())
	{
		MaterialAssetName = FPackageName::GetShortName(MergedAssetPackageName);
		MaterialPackageName = FPackageName::GetLongPackagePath(MergedAssetPackageName) + TEXT("/") + MaterialAssetName;
	}
	else
	{
		MaterialAssetName = FPackageName::GetShortName(InBasePackageName);
		MaterialPackageName = FPackageName::GetLongPackagePath(InBasePackageName) + TEXT("/") + MaterialAssetName;
	}

	UPackage* MaterialPackage = InOuter;
	if (MaterialPackage == nullptr)
	{
		MaterialPackage = CreatePackage(nullptr, *MaterialPackageName);
		check(MaterialPackage);
		MaterialPackage->FullyLoad();
		MaterialPackage->Modify();
	}

	UMaterialInstanceConstant* MergedMaterial = ProxyMaterialUtilities::CreateProxyMaterialInstance(MaterialPackage, InSettings.MaterialSettings, OutMaterial, MaterialAssetName, MaterialPackageName, OutAssetsToSync);
	// Set material static lighting usage flag if project has static lighting enabled
	/*static const auto AllowStaticLightingVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.AllowStaticLighting"));
	const bool bAllowStaticLighting = (!AllowStaticLightingVar || AllowStaticLightingVar->GetValueOnGameThread() != 0);
	if (bAllowStaticLighting)
	{
	MergedMaterial->CheckMaterialUsage(MATUSAGE_StaticLighting);
	}

	return MergedMaterial;*/
	return;
}

void ScaleTextureCoordinatesToBox_new(const FBox2D& Box, TArray<FVector2D>& InOutTextureCoordinates)
{
	const FBox2D CoordinateBox(InOutTextureCoordinates);
	const FVector2D CoordinateRange = CoordinateBox.GetSize();
	const FVector2D Offset = CoordinateBox.Min + Box.Min;
	const FVector2D Scale = Box.GetSize() / CoordinateRange;
	for (FVector2D& Coordinate : InOutTextureCoordinates)
	{
		Coordinate = (Coordinate - Offset) * Scale;
	}
}

void GetMultipleMaterial::DetermineMaterialVertexDataUsage_new(TArray<bool>& InOutMaterialUsesVertexData, const TArray<UMaterialInterface*>& UniqueMaterials, const UMaterialOptions* MaterialOptions)
{
	InOutMaterialUsesVertexData.SetNum(UniqueMaterials.Num());
	for (int32 MaterialIndex = 0; MaterialIndex < UniqueMaterials.Num(); ++MaterialIndex)
	{
		UMaterialInterface* Material = UniqueMaterials[MaterialIndex];
		for (const FPropertyEntry& Entry : MaterialOptions->Properties)
		{
			// Don't have to check a property if the result is going to be constant anyway
			if (!Entry.bUseConstantValue && Entry.Property != MP_MAX)
			{
				int32 NumTextureCoordinates;
				bool bUsesVertexData;
				Material->AnalyzeMaterialProperty(Entry.Property, NumTextureCoordinates, bUsesVertexData);

				if (bUsesVertexData || NumTextureCoordinates > 1)
				{
					InOutMaterialUsesVertexData[MaterialIndex] = true;
					break;
				}
			}
		}
	}
}

void PopulatePropertyEntry_new(const FMaterialProxySettings& MaterialSettings, EMaterialProperty MaterialProperty, FPropertyEntry& InOutPropertyEntry)
{
	InOutPropertyEntry.Property = MaterialProperty;
	switch (MaterialSettings.TextureSizingType)
	{
		/** Set property output size to unique per-property user set sizes */
	case TextureSizingType_UseManualOverrideTextureSize:
	{
		InOutPropertyEntry.bUseCustomSize = true;
		InOutPropertyEntry.CustomSize = [MaterialSettings, MaterialProperty]() -> FIntPoint
		{
			switch (MaterialProperty)
			{
			case MP_BaseColor: return MaterialSettings.DiffuseTextureSize;
			case MP_Specular: return MaterialSettings.SpecularTextureSize;
			case MP_Roughness: return MaterialSettings.RoughnessTextureSize;
			case MP_Metallic: return MaterialSettings.MetallicTextureSize;
			case MP_Normal: return MaterialSettings.NormalTextureSize;
			case MP_Opacity: return MaterialSettings.OpacityTextureSize;
			case MP_OpacityMask: return MaterialSettings.OpacityMaskTextureSize;
			case MP_EmissiveColor: return MaterialSettings.EmissiveTextureSize;
			case MP_AmbientOcclusion: return MaterialSettings.AmbientOcclusionTextureSize;
			default:
			{
				checkf(false, TEXT("Invalid Material Property"));
				return FIntPoint();
			}
			}
		}();

		break;
	}
	/** Set property output size to biased values off the TextureSize value (Normal at fullres, Diffuse at halfres, and anything else at quarter res */
	case TextureSizingType_UseAutomaticBiasedSizes:
	{
		const FIntPoint FullRes = MaterialSettings.TextureSize;
		const FIntPoint HalfRes = FIntPoint(FMath::Max(8, FullRes.X >> 1), FMath::Max(8, FullRes.Y >> 1));
		const FIntPoint QuarterRes = FIntPoint(FMath::Max(4, FullRes.X >> 2), FMath::Max(4, FullRes.Y >> 2));

		InOutPropertyEntry.bUseCustomSize = true;
		InOutPropertyEntry.CustomSize = [FullRes, HalfRes, QuarterRes, MaterialSettings, MaterialProperty]() -> FIntPoint
		{
			switch (MaterialProperty)
			{
			case MP_Normal: return FullRes;
			case MP_BaseColor: return HalfRes;
			case MP_Specular: return QuarterRes;
			case MP_Roughness: return QuarterRes;
			case MP_Metallic: return QuarterRes;
			case MP_Opacity: return QuarterRes;
			case MP_OpacityMask: return QuarterRes;
			case MP_EmissiveColor: return QuarterRes;
			case MP_AmbientOcclusion: return QuarterRes;
			default:
			{
				checkf(false, TEXT("Invalid Material Property"));
				return FIntPoint();
			}
			}
		}();

		break;
	}
	/** Set all sizes to TextureSize */
	case TextureSizingType_UseSingleTextureSize:
	case TextureSizingType_UseSimplygonAutomaticSizing:
	{
		InOutPropertyEntry.bUseCustomSize = false;
		InOutPropertyEntry.CustomSize = MaterialSettings.TextureSize;
		break;
	}
	}
	/** Check whether or not a constant value should be used for this property */
	InOutPropertyEntry.bUseConstantValue = [MaterialSettings, MaterialProperty]() -> bool
	{
		switch (MaterialProperty)
		{
		case MP_BaseColor: return false;
		case MP_Normal: return !MaterialSettings.bNormalMap;
		case MP_Specular: return !MaterialSettings.bSpecularMap;
		case MP_Roughness: return !MaterialSettings.bRoughnessMap;
		case MP_Metallic: return !MaterialSettings.bMetallicMap;
		case MP_Opacity: return !MaterialSettings.bOpacityMap;
		case MP_OpacityMask: return !MaterialSettings.bOpacityMaskMap;
		case MP_EmissiveColor: return !MaterialSettings.bEmissiveMap;
		case MP_AmbientOcclusion: return !MaterialSettings.bAmbientOcclusionMap;
		default:
		{
			checkf(false, TEXT("Invalid Material Property"));
			return false;
		}
		}
	}();
	/** Set the value if a constant value should be used for this property */
	InOutPropertyEntry.ConstantValue = [MaterialSettings, MaterialProperty]() -> float
	{
		switch (MaterialProperty)
		{
		case MP_BaseColor: return 1.0f;
		case MP_Normal: return 1.0f;
		case MP_Specular: return MaterialSettings.SpecularConstant;
		case MP_Roughness: return MaterialSettings.RoughnessConstant;
		case MP_Metallic: return MaterialSettings.MetallicConstant;
		case MP_Opacity: return MaterialSettings.OpacityConstant;
		case MP_OpacityMask: return MaterialSettings.OpacityMaskConstant;
		case MP_EmissiveColor: return 0.0f;
		case MP_AmbientOcclusion: return MaterialSettings.AmbientOcclusionConstant;
		default:
		{
			checkf(false, TEXT("Invalid Material Property"));
			return 1.0f;
		}
		}
	}();
}

UMaterialOptions* GetMultipleMaterial::PopulateMaterialOptions_new(const FMaterialProxySettings& MaterialSettings)
{
	UMaterialOptions* MaterialOptions = DuplicateObject(GetMutableDefault<UMaterialOptions>(), GetTransientPackage());
	MaterialOptions->Properties.Empty();
	MaterialOptions->TextureSize = MaterialSettings.TextureSize;

	const bool bCustomSizes = MaterialSettings.TextureSizingType == TextureSizingType_UseManualOverrideTextureSize;

	FPropertyEntry Property;
	PopulatePropertyEntry_new(MaterialSettings, MP_BaseColor, Property);
	MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_Specular, Property);
	if (MaterialSettings.bSpecularMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_Roughness, Property);
	if (MaterialSettings.bRoughnessMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_Metallic, Property);
	if (MaterialSettings.bMetallicMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_Normal, Property);
	if (MaterialSettings.bNormalMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_Opacity, Property);
	if (MaterialSettings.bOpacityMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_OpacityMask, Property);
	if (MaterialSettings.bOpacityMaskMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_EmissiveColor, Property);
	if (MaterialSettings.bEmissiveMap)
		MaterialOptions->Properties.Add(Property);

	PopulatePropertyEntry_new(MaterialSettings, MP_AmbientOcclusion, Property);
	if (MaterialSettings.bAmbientOcclusionMap)
		MaterialOptions->Properties.Add(Property);

	return MaterialOptions;
}

void ExtractPhysicsDataFromComponents_new(const TArray<UPrimitiveComponent*>& ComponentsToMerge, TArray<FKAggregateGeom>& InOutPhysicsGeometry, UBodySetup*& OutBodySetupSource)
{
	InOutPhysicsGeometry.AddDefaulted(ComponentsToMerge.Num());
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentsToMerge.Num(); ++ComponentIndex)
	{
		UPrimitiveComponent* PrimComp = ComponentsToMerge[ComponentIndex];
		UBodySetup* BodySetup = nullptr;
		FTransform ComponentToWorld = FTransform::Identity;

		if (UStaticMeshComponent* StaticMeshComp = Cast<UStaticMeshComponent>(PrimComp))
		{
			UStaticMesh* SrcMesh = StaticMeshComp->GetStaticMesh();
			if (SrcMesh)
			{
				BodySetup = SrcMesh->BodySetup;
			}
			ComponentToWorld = StaticMeshComp->GetComponentToWorld();
		}
		else if (UShapeComponent* ShapeComp = Cast<UShapeComponent>(PrimComp))
		{
			BodySetup = ShapeComp->GetBodySetup();
			ComponentToWorld = ShapeComp->GetComponentToWorld();
		}

		//add
		USplineMeshComponent* SplineMeshComponent = Cast<USplineMeshComponent>(PrimComp);
		FMeshMergeHelpers::ExtractPhysicsGeometry(BodySetup, ComponentToWorld, SplineMeshComponent != nullptr, InOutPhysicsGeometry[ComponentIndex]);
		if (SplineMeshComponent)
		{
			FMeshMergeHelpers::PropagateSplineDeformationToPhysicsGeometry(SplineMeshComponent, InOutPhysicsGeometry[ComponentIndex]);
		}

		// We will use first valid BodySetup as a source of physics settings
		if (OutBodySetupSource == nullptr)
		{
			OutBodySetupSource = BodySetup;
		}
	}
}

float FlattenEmissivescale_new(TArray<struct FFlattenMaterial>& InMaterialList)
{
	// Find maximum emissive scaling value across materials
	float MaxScale = 0.0f;
	for (const FFlattenMaterial& Material : InMaterialList)
	{
		MaxScale = FMath::Max(MaxScale, Material.EmissiveScale);
	}

	// Renormalize samples
	const float Multiplier = 1.0f / MaxScale;
	const int32 NumThreads = [&]()
	{
		return FPlatformProcess::SupportsMultithreading() ? FPlatformMisc::NumberOfCores() : 1;
	}();

	const int32 MaterialsPerThread = FMath::CeilToInt((float)InMaterialList.Num() / (float)NumThreads);
	ParallelFor(NumThreads, [&InMaterialList, MaterialsPerThread, Multiplier, MaxScale]
	(int32 Index)
	{
		int32 StartIndex = FMath::CeilToInt((Index)* MaterialsPerThread);
		const int32 EndIndex = FMath::Min(FMath::CeilToInt((Index + 1) * MaterialsPerThread), InMaterialList.Num());

		for (; StartIndex < EndIndex; ++StartIndex)
		{
			FFlattenMaterial& Material = InMaterialList[StartIndex];
			if (Material.EmissiveScale != MaxScale)
			{
				for (FColor& Sample : Material.GetPropertySamples(EFlattenMaterialProperties::Emissive))
				{
					Sample.R = Sample.R * Multiplier;
					Sample.G = Sample.G * Multiplier;
					Sample.B = Sample.B * Multiplier;
					Sample.A = Sample.A * Multiplier;
				}
			}
		}
	}, NumThreads == 1);

	return MaxScale;
}

FIntPoint ConditionalImageResize_new(const FIntPoint& SrcSize, const FIntPoint& DesiredSize, TArray<FColor>& InOutImage, bool bLinearSpace)
{
	const int32 NumDesiredSamples = DesiredSize.X*DesiredSize.Y;
	if (InOutImage.Num() && InOutImage.Num() != NumDesiredSamples)
	{
		check(InOutImage.Num() == SrcSize.X*SrcSize.Y);
		TArray<FColor> OutImage;
		if (NumDesiredSamples > 0)
		{
			FImageUtils::ImageResize(SrcSize.X, SrcSize.Y, InOutImage, DesiredSize.X, DesiredSize.Y, OutImage, bLinearSpace);
		}
		Exchange(InOutImage, OutImage);
		return DesiredSize;
	}

	return SrcSize;
}

void CopyTextureRect_new(const FColor* Src, const FIntPoint& SrcSize, FColor* Dst, const FIntPoint& DstSize, const FIntPoint& DstPos)
{
	const int32 RowLength = SrcSize.X * sizeof(FColor);
	FColor* RowDst = Dst + DstSize.X*DstPos.Y;
	const FColor* RowSrc = Src;

	for (int32 RowIdx = 0; RowIdx < SrcSize.Y; ++RowIdx)
	{
		FMemory::Memcpy(RowDst + DstPos.X, RowSrc, RowLength);
		RowDst += DstSize.X;
		RowSrc += SrcSize.X;
	}
}

void GetMultipleMaterial::FlattenBinnedMaterials_new(TArray<struct FFlattenMaterial>& InMaterialList, FFlattenMaterial MaterialSetting, TArray<struct FFlattenMaterial>& OutMergedMaterial)//, TArray<FUVOffsetScalePair>& OutUVTransforms)
{
	//OutUVTransforms.AddZeroed(InMaterialList.Num());
	// Flatten emissive scale across all incoming materials
	for (int index = 0; index < InMaterialList.Num(); index++)
	{
		OutMergedMaterial.Add(MaterialSetting);
		//OutMergedMaterial[index] = MaterialSetting;
		OutMergedMaterial[index].EmissiveScale = FlattenEmissivescale_new(InMaterialList);
	}
	//OutMergedMaterial.EmissiveScale = FlattenEmissivescale_new(InMaterialList);

	// Merge all material properties
	for (int32 Index = 0; Index < (int32)EFlattenMaterialProperties::NumFlattenMaterialProperties; ++Index)
	{
		const EFlattenMaterialProperties Property = (EFlattenMaterialProperties)Index;
		const FIntPoint& OutTextureSize = MaterialSetting.GetPropertySize(Property);
		if (OutTextureSize != FIntPoint::ZeroValue)
		{
			/*
			TArray<FColor>& OutSamples = MaterialSetting.GetPropertySamples(Property);
			OutSamples.Reserve(OutTextureSize.X * OutTextureSize.Y);
			OutSamples.SetNumZeroed(OutTextureSize.X * OutTextureSize.Y);
			*/

			for (int32 MaterialIndex = 0; MaterialIndex < InMaterialList.Num(); ++MaterialIndex)
			{
				bool bMaterialsWritten = false;
				TArray<FColor>& OutSamples = OutMergedMaterial[MaterialIndex].GetPropertySamples(Property);
				OutSamples.Reserve(OutTextureSize.X * OutTextureSize.Y);
				OutSamples.SetNumZeroed(OutTextureSize.X * OutTextureSize.Y);

				// Determine output size and offset
				FFlattenMaterial& FlatMaterial = InMaterialList[MaterialIndex];
				OutMergedMaterial[MaterialIndex].bDitheredLODTransition |= FlatMaterial.bDitheredLODTransition;
				OutMergedMaterial[MaterialIndex].bTwoSided |= FlatMaterial.bTwoSided;

				if (FlatMaterial.DoesPropertyContainData(Property))
				{
					//const FBox2D MaterialBox = InMaterialBoxes[MaterialIndex];
					const FIntPoint& InputSize = FlatMaterial.GetPropertySize(Property);
					TArray<FColor>& InputSamples = FlatMaterial.GetPropertySamples(Property);

					// Resize material to match output (area) size
					FIntPoint OutputSize = FIntPoint(OutTextureSize.X, OutTextureSize.Y);
					ConditionalImageResize_new(InputSize, OutputSize, InputSamples, false);

					// Copy material data to the merged 'atlas' texture
					//FIntPoint OutputPosition = FIntPoint(OutTextureSize.X , OutTextureSize.Y );
					FIntPoint OutputPosition = FIntPoint(0, 0);
					CopyTextureRect_new(InputSamples.GetData(), OutputSize, OutSamples.GetData(), OutTextureSize, OutputPosition);

					// Set the UV tranforms only once
					/*if (Index == 0)
					{
					FUVOffsetScalePair& UVTransform = OutUVTransforms[MaterialIndex];
					UVTransform.Key = MaterialBox.Min;
					UVTransform.Value = MaterialBox.GetSize();
					}*/

					bMaterialsWritten = true;
				}
				if (!bMaterialsWritten)
				{
					OutSamples.Empty();
					for (int index = 0; index < InMaterialList.Num(); index++)
					{
						OutMergedMaterial[index].SetPropertySize(Property, FIntPoint(0, 0));
					}
				}
			}
		}
	}
}

void GetMultipleMaterial::AssembleListOfExporters(TArray<UExporter*>& OutExporters)
{
	auto TransientPackage = GetTransientPackage();

	// @todo DB: Assemble this set once.
	OutExporters.Empty();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UExporter::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			UExporter* Exporter = NewObject<UExporter>(TransientPackage, *It);
			OutExporters.Add(Exporter);
		}
	}
}

TMap<FString, FString> GetMultipleMaterial::ExportObjectsToBMP(TArray<UObject*>& ObjectsToExport)
{
	TMap<FString, FString> mtls;
	TArray<UExporter*> Exporters;
	AssembleListOfExporters(Exporters);

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

		//name = name.Replace(TEXT("_Diffuse"), TEXT(""))
		//	/*.Replace(TEXT("_MRS"), TEXT(""))*/
		//	.Replace(TEXT("_Normal"), TEXT(""));

		FString Filename = /*ProjectPath +*/ TEXT("maps/") + name /*+ TEXT("_") + name*/;

		switch (Index)
		{
		case 0:
			MapType = TEXT("name ");
			break;
		case 1:
			//Filename += TEXT("_D");
			MapType = TEXT("map_Kd ");
			break;
		case 2:
			//Filename += TEXT("_N");
			MapType = TEXT("norm ");
			break;
		case 3:
			MapType = TEXT("map_Tr ");
			break;
		case 4:
			MapType = TEXT("map_TrMask ");
			break;
			/*case 5:
				MapType = TEXT("map_Ao ");
				break;*/
		case 5://MRS
			mtls.FindOrAdd(TEXT("map_Ks ")) = Filename + TEXT(".bmp");
			//auto&S = mtls.FindOrAdd(TEXT("map_Ks "));
			//S = Filename + TEXT(".bmp");
			//roughness
			mtls.FindOrAdd(TEXT("map_Pr ")) = Filename + TEXT(".bmp");
			//R = Filename + TEXT(".bmp");
			//metalics
			MapType = TEXT("map_Pm ");
			/*auto&T = mtls.FindOrAdd(TEXT("map_Pm "));
			T = Filename + TEXT(".bmp");*/
			break;
		case 6:
			//Filename += TEXT("_S");
			MapType = TEXT("map_Ke ");
			break;
		default:
			break;
		}
		Filename += TEXT(".bmp");

		auto&T = mtls.FindOrAdd(MapType);
		T = Filename;

		Filename = ProjectPath + TEXT("GetObjandMaterial/") + Filename;
		//FString Filename = ProjectPath+ TEXT("GetMaterial/")+ObjectToExport->GetName()+ TEXT(".bmp");

		//UAssetExportTask Params;
		////UExporter::FExportToFileParams Params;
		//Params.Object = ObjectToExport;
		////Params.Exporter = ExporterToUse;
		//Params.Exporter = ExporterUse;
		////Params.Filename = *SaveFileName;
		//Params.Filename = *Filename;
		//Params.bSelected = false;
		////Params.InSelectedOnly = false;
		//Params.bReplaceIdentical = true;
		////Params.NoReplaceIdentical = false;
		//Params.bPrompt = false;
		////Params.Prompt = false;
		//Params.bUseFileArchive = ObjectToExport->IsA(UPackage::StaticClass());
		//Params.bWriteEmptyFiles = false;
		//Params.WriteEmptyFiles = false;
		//UExporter::RunAssetExportTask(&Params);
		//UExporter::ExportToFileEx(Params);
		UExporter::ExportToFile(ObjectToExport, ExporterUse, *Filename, false, false, false);
	}
	ObjectsToExport.Empty();
	return mtls;
}

void MergeComponentsToStaticMesh_new(UPrimitiveComponent* Component, FMeshMergingSettings& InSettings, UPackage* InOuter, FString& InBasePackageName, TArray<UObject*>& OutAssetsToSync, FVector& OutMergedActorLocation, const float ScreenSize, bool bSilent /*= false*/)
{
	// Use first mesh for naming and pivot
	//bool bFirstMesh = true;
	FString MergedAssetPackageName;
	TArray<UObject*> temp;
	//OutAssetsToSync.
	//FVector MergedAssetPivot;

	//int LODNum = Cast<UStaticMeshComponent>Component;

	UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Component);
	MergedAssetPackageName = MeshComponent->GetStaticMesh()->GetOutermost()->GetName();
	int LODNum = MeshComponent->GetStaticMesh()->GetNumLODs();
	//int LODNum = MeshComponent->LODData.Num();
	// Nothing to do if no StaticMeshComponents
	/*if (StaticMeshComponentsToMerge.Num() == 0)
	{
		return;
		Cast<UStaticMeshComponent>(Component)->GetStaticMesh()->GetNumLODs()
	}*/

	//

	const bool bMergeMaterialData = InSettings.bMergeMaterials && InSettings.LODSelectionType != EMeshLODSelectionType::AllLODs;
	const bool bPropagateMeshData = InSettings.bBakeVertexDataToMesh || (bMergeMaterialData && InSettings.bUseVertexDataForBakingMaterial);

	for (int32 index = 0; index < LODNum; index++)
	{
		FMeshMergeDataTracker DataTracker;
		TArray<FStaticMeshComponentAdapter> Adapters;
		TArray<FSectionInfo> Sections;
		{
			// Adding LOD 0 for merged mesh output
			DataTracker.AddLODIndex(0);

			// Retrieve mesh and section data for each component
			{
				Adapters.Add(FStaticMeshComponentAdapter(MeshComponent));
				FStaticMeshComponentAdapter& Adapter = Adapters.Last();

				int32 LODIndex = index;

				// Retrieve raw mesh data
				//add
				FMeshDescription& RawMesh = DataTracker.AddAndRetrieveRawMesh(0, LODIndex, MeshComponent->GetStaticMesh());
				Adapter.RetrieveRawMeshData(LODIndex, RawMesh, bPropagateMeshData);

				// Reset section for reuse
				Sections.SetNum(0, false);

				// Extract sections for given LOD index from the mesh
				Adapter.RetrieveMeshSections(LODIndex, Sections);

				for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
				{
					const FSectionInfo& Section = Sections[SectionIndex];
					// Unique section index for remapping
					const int32 UniqueIndex = DataTracker.AddSection(Section);

					// Store of original to unique section index entry for this component + LOD index
					DataTracker.AddSectionRemapping(0, LODIndex, Section.MaterialIndex, UniqueIndex);
					DataTracker.AddMaterialSlotName(Section.Material, Section.MaterialSlotName);
				}
			}
		}
		DataTracker.ProcessRawMeshes();

		// Find all unique materials and remap section to unique materials
		TArray<UMaterialInterface*> UniqueMaterials;
		TMap<UMaterialInterface*, int32> MaterialIndices;
		TMultiMap<uint32, uint32> SectionToMaterialMap;

		for (int32 SectionIndex = 0; SectionIndex < DataTracker.NumberOfUniqueSections(); ++SectionIndex)
		{
			// Unique index for material
			const int32 UniqueIndex = UniqueMaterials.AddUnique(DataTracker.GetMaterialForSectionIndex(SectionIndex));

			// Store off usage of unique material by unique sections
			SectionToMaterialMap.Add(UniqueIndex, SectionIndex);
		}

		// If the user wants to merge materials into a single one
		{
			UMaterialOptions* MaterialOptions = GetMultipleMaterial::PopulateMaterialOptions_new(InSettings.MaterialSettings);
			// Check each material to see if the shader actually uses vertex data and collect flags
			TArray<bool> bMaterialUsesVertexData;
			GetMultipleMaterial::DetermineMaterialVertexDataUsage_new(bMaterialUsesVertexData, UniqueMaterials, MaterialOptions);

			TArray<FMeshData> GlobalMeshSettings;
			TArray<FMaterialData> GlobalMaterialSettings;
			TArray<float> SectionMaterialImportanceValues;

			//TMultiMap< FMeshLODKey, MaterialRemapPair > OutputMaterialsMap;

			TMap<EMaterialProperty, FIntPoint> PropertySizes;
			for (const FPropertyEntry& Entry : MaterialOptions->Properties)
			{
				if (!Entry.bUseConstantValue && Entry.Property != MP_MAX)
				{
					PropertySizes.Add(Entry.Property, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
				}
			}

			TMap<UMaterialInterface*, int32> MaterialToDefaultMeshData;

			for (TConstRawMeshIterator RawMeshIterator = DataTracker.GetConstRawMeshIterator(); RawMeshIterator; ++RawMeshIterator)
			{
				const FMeshLODKey& Key = RawMeshIterator.Key();
				//add
				const FMeshDescription& RawMesh = RawMeshIterator.Value();
				const bool bRequiresUniqueUVs = DataTracker.DoesMeshLODRequireUniqueUVs(Key);

				// Retrieve all sections and materials for key
				TArray<SectionRemapPair> SectionRemapPairs;
				DataTracker.GetMappingsForMeshLOD(Key, SectionRemapPairs);
				if (SectionRemapPairs.Num() > 1)
				{
					int size = SectionRemapPairs.Num();
					for (int sectionIndex = 0; sectionIndex < size / 2; sectionIndex++)
					{
						auto temp = SectionRemapPairs[sectionIndex];
						SectionRemapPairs[sectionIndex] = SectionRemapPairs[size - sectionIndex - 1];
						SectionRemapPairs[size - sectionIndex - 1] = temp;
					}
				}

				// Contains unique materials used for this key, and the accompanying section index which point to the material
				TMap<UMaterialInterface*, TArray<int32>> MaterialAndSectionIndices;

				for (const SectionRemapPair& RemapPair : SectionRemapPairs)
				{
					const int32 UniqueIndex = RemapPair.Value;
					const int32 SectionIndex = RemapPair.Key;
					TArray<int32>& SectionIndices = MaterialAndSectionIndices.FindOrAdd(DataTracker.GetMaterialForSectionIndex(UniqueIndex));
					SectionIndices.Add(SectionIndex);
				}

				// Cache unique texture coordinates
				TArray<FVector2D> UniqueTextureCoordinates;

				for (TPair<UMaterialInterface*, TArray<int32>>& MaterialSectionIndexPair : MaterialAndSectionIndices)
				{
					UMaterialInterface* Material = MaterialSectionIndexPair.Key;
					const int32 MaterialIndex = UniqueMaterials.IndexOfByKey(Material);
					const TArray<int32>& SectionIndices = MaterialSectionIndexPair.Value;
					const bool bDoesMaterialUseVertexData = bMaterialUsesVertexData[MaterialIndex];

					FMaterialData MaterialData;
					MaterialData.Material = Material;
					MaterialData.PropertySizes = PropertySizes;

					FMeshData MeshData;
					int32 MeshDataIndex = 0;

					{
						//add
						MeshData.RawMeshDescription = nullptr;
						MeshData.TextureCoordinateBox = FBox2D(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f));

						// This prevents baking out the same material multiple times, which would be wasteful when it does not use vertex data anyway
						const bool bPreviouslyAdded = MaterialToDefaultMeshData.Contains(Material);
						int32& DefaultMeshDataIndex = MaterialToDefaultMeshData.FindOrAdd(Material);

						if (!bPreviouslyAdded)
						{
							DefaultMeshDataIndex = GlobalMeshSettings.Num();
							GlobalMeshSettings.Add(MeshData);
							GlobalMaterialSettings.Add(MaterialData);
						}

						MeshDataIndex = DefaultMeshDataIndex;
					}
				}
			}

			TArray<FMeshData*> MeshSettingPtrs;
			for (int32 SettingsIndex = 0; SettingsIndex < GlobalMeshSettings.Num(); ++SettingsIndex)
			{
				MeshSettingPtrs.Add(&GlobalMeshSettings[SettingsIndex]);
			}

			TArray<FMaterialData*> MaterialSettingPtrs;
			for (int32 SettingsIndex = 0; SettingsIndex < GlobalMaterialSettings.Num(); ++SettingsIndex)
			{
				MaterialSettingPtrs.Add(&GlobalMaterialSettings[SettingsIndex]);
			}

			TArray<FBakeOutput> BakeOutputs;
			IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
			Module.BakeMaterials(MaterialSettingPtrs, MeshSettingPtrs, BakeOutputs);

			//add
			// Append constant properties ?
			TArray<FColor> ConstantData;
			FIntPoint ConstantSize(1, 1);
			for (const FPropertyEntry& Entry : MaterialOptions->Properties)
			{
				if (Entry.bUseConstantValue && Entry.Property != MP_MAX)
				{
					ConstantData.SetNum(1, false);
					ConstantData[0] = FLinearColor(Entry.ConstantValue, Entry.ConstantValue, Entry.ConstantValue).ToFColor(true);
					for (FBakeOutput& Ouput : BakeOutputs)
					{
						Ouput.PropertyData.Add(Entry.Property, ConstantData);
						Ouput.PropertySizes.Add(Entry.Property, ConstantSize);
					}
				}
			}

			TArray<FFlattenMaterial> FlattenedMaterials;
			GetMultipleMaterial::ConvertOutputToFlatMaterials_new(BakeOutputs, GlobalMaterialSettings, FlattenedMaterials);

			// Try to optimize materials where possible
			for (FFlattenMaterial& InMaterial : FlattenedMaterials)
			{
				FMaterialUtilities::OptimizeFlattenMaterial(InMaterial);
			}

			FFlattenMaterial OutMaterialSetting;
			for (const FPropertyEntry& Entry : MaterialOptions->Properties)
			{
				if (Entry.Property != MP_MAX)
				{
					EFlattenMaterialProperties OldProperty = GetMultipleMaterial::NewToOldProperty_new(Entry.Property);
					OutMaterialSetting.SetPropertySize(OldProperty, Entry.bUseCustomSize ? Entry.CustomSize : MaterialOptions->TextureSize);
				}
			}

			TArray<FFlattenMaterial> outFlattenedMaterials;

			GetMultipleMaterial::FlattenBinnedMaterials_new(FlattenedMaterials, OutMaterialSetting, outFlattenedMaterials);

			FAssetRegistryModule& AssetRegistry = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

			for (int32 SectionIndex = 0; SectionIndex < MaterialSettingPtrs.Num(); ++SectionIndex)
			{
				InBasePackageName = MergedAssetPackageName = FPackageName::GetLongPackagePath(InBasePackageName) + '/' + (Sections[SectionIndex].MaterialSlotName).ToString() + FString("_LOD" + FString::FromInt(index));
				GetMultipleMaterial::CreateProxyMaterial_new(InBasePackageName, MergedAssetPackageName, InOuter, InSettings, outFlattenedMaterials[SectionIndex], OutAssetsToSync);
				{
					GetMultipleMaterial::ExportObjectsToBMP(OutAssetsToSync);
				}
			}
		}
	}
	return;
}

void GetMultipleMaterial::run(UPrimitiveComponent* Component, FString PathName, FString Name, FMeshMergingSettings settings)
{
	FVector Local;
	TArray<UObject*> AssetsToSync;
	FString PathAndName = PathName + Name;

	MergeComponentsToStaticMesh_new(Component, settings, nullptr, PathAndName, AssetsToSync, Local, 1.0, false);
}