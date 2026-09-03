#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

class UBlueprint;
class UMaterial;
class UMaterialFunction;
class UMaterialInstanceConstant;
struct FAssetData;

// Private (core-module only) pieces of the text-mirror implementation. The
// exported helpers shared with the Niagara bridge live in UeNodeNexusBridgeTranscodeApi.h.
namespace UeNodeNexusBridge::Transcode
{
// --- raw builders ----------------------------------------------------------
TSharedPtr<FJsonObject> BuildMaterialRaw(UMaterial* Material);
TSharedPtr<FJsonObject> BuildMaterialFunctionRaw(UMaterialFunction* Function);
TSharedPtr<FJsonObject> BuildMaterialInstanceRaw(UMaterialInstanceConstant* Instance);
TSharedPtr<FJsonObject> BuildBlueprintRaw(UBlueprint* Blueprint);
TSharedPtr<FJsonObject> BuildGenericRaw(UObject* Asset);
TSharedPtr<FJsonObject> BuildStubRaw(const FAssetData& AssetData);
// Dispatch by kind; nullptr when the asset kind is not exportable here.
TSharedPtr<FJsonObject> BuildRawForAsset(UObject* Asset, const FString& Kind);

// --- plan appliers ---------------------------------------------------------
void ApplyMaterialPlan(UObject* Owner, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context);
void ApplyMaterialInstancePlan(UMaterialInstanceConstant* Instance, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context);
void ApplyBlueprintPlan(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context);
void ApplyGenericPlan(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context);
// Creates a new asset of the given kind at asset_path; nullptr + error on failure.
UObject* CreateAssetForKind(const FString& AssetPath, const FString& Kind, const FString& AssetClass, FString& OutError);

// --- schema pieces (K2 lives in its own TU) --------------------------------
TSharedPtr<FJsonObject> BuildK2NodeSchema();
TSharedPtr<FJsonObject> BuildFunctionSignatureRecord(UFunction* Function);
}
