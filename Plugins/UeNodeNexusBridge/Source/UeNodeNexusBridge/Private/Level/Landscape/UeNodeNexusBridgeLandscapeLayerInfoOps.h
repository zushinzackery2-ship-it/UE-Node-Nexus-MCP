#pragma once

#include "CoreMinimal.h"

class ALandscapeProxy;
class FJsonObject;
class ULandscapeLayerInfoObject;
class UPackage;

namespace UeNodeNexusBridge
{
struct FLandscapeLayerInfoPlan
{
    FName LayerName;
    FString LayerInfoObjectPath;
    FString LayerInfoPackageName;
    FString LayerInfoAssetName;
    FString OldLayerInfoPath;
    FString ExistingLayerInfoLayerName;
    ULandscapeLayerInfoObject* ExistingLayerInfo = nullptr;
    bool bTargetLayerExists = false;
    bool bCreateIfMissing = false;
    bool bLayerInfoExists = false;
    bool bLayerInfoCreated = false;
    bool bSetNoWeightBlend = false;
    bool bNoWeightBlend = false;
};

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message);

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Details);

ALandscapeProxy* ResolveLandscapeProxyForLayerInfo(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutError);

bool BuildLandscapeLayerInfoPlan(
    ALandscapeProxy* Landscape,
    const TSharedPtr<FJsonObject>& LayerPayload,
    int32 Index,
    const FString& Operation,
    const FString& RequestId,
    FLandscapeLayerInfoPlan& OutPlan,
    TSharedPtr<FJsonObject>& OutError);

bool ApplyLandscapeLayerInfoPlan(
    ALandscapeProxy* Landscape,
    FLandscapeLayerInfoPlan& Plan,
    TArray<UPackage*>& OutPackagesToSave);

void RefreshLandscapeLayerInfoEditorState(ALandscapeProxy* Landscape);

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoDiff(
    const TArray<FLandscapeLayerInfoPlan>& Plans,
    bool bApplied);

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoSavedPackagesJson(
    const TArray<UPackage*>& Packages,
    bool bSaved);

bool AnyLandscapeLayerInfoPlanChanges(const TArray<FLandscapeLayerInfoPlan>& Plans);
}
