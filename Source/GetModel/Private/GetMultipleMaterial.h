#pragma once
#include "Components/PrimitiveComponent.h"
#include "Engine/MeshMerging.h"
#include "Exporters/Exporter.h"
#include "MaterialBaking/Public/MaterialOptions.h"

class GetMultipleMaterial
{
public:
	GetMultipleMaterial();
	~GetMultipleMaterial();
	static void run(UPrimitiveComponent* Component, FString PathName, FString Name, FMeshMergingSettings settings);
	static void AssembleListOfExporters(TArray<UExporter*>& OutExporters);
	static void ExportMaterialArray(UPrimitiveComponent* Component, FMeshMergingSettings settings, TArray<UObject*>& OutAssetsToSync, FString& InBasePackageName);

	static TMap<FString, FString> ExportObjectsToBMP(TArray<UObject*>& ObjectsToExport);

	static UMaterialOptions* PopulateMaterialOptions_new(const FMaterialProxySettings& MaterialSettings);

	static void DetermineMaterialVertexDataUsage_new(TArray<bool>& InOutMaterialUsesVertexData, const TArray<UMaterialInterface*>& UniqueMaterials, const UMaterialOptions* MaterialOptions);

	static void ConvertOutputToFlatMaterials_new(const TArray<FBakeOutput>& BakeOutputs, const TArray<FMaterialData>& MaterialData, TArray<FFlattenMaterial> &FlattenedMaterials);

	static EFlattenMaterialProperties NewToOldProperty_new(int32 NewProperty);

	static void FlattenBinnedMaterials_new(TArray<struct FFlattenMaterial>& InMaterialList, FFlattenMaterial MaterialSetting, TArray<struct FFlattenMaterial>& OutMergedMaterial);

	static void CreateProxyMaterial_new(const FString &InBasePackageName, FString MergedAssetPackageName, UPackage* InOuter, const FMeshMergingSettings &InSettings, FFlattenMaterial OutMaterial, TArray<UObject *>& OutAssetsToSync);

	//static void run(const TArray<UPrimitiveComponent*> &Component, FString PathName);
};
