#include "UeNodeNexusBridgeLandscapeLayerInfoOps.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "UeNodeNexusBridgeAssetPaths.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
FString LayerInfoPath(ULandscapeLayerInfoObject* LayerInfo)
{
    return LayerInfo != nullptr ? LayerInfo->GetPathName() : FString();
}

ALandscapeProxy* TargetLayerOwnerFor(ALandscapeProxy* Landscape)
{
    if (Landscape == nullptr)
    {
        return nullptr;
    }

    if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
    {
        if (LandscapeInfo->LandscapeActor.IsValid())
        {
            return LandscapeInfo->LandscapeActor.Get();
        }
    }

    return Landscape;
}

TSharedPtr<FJsonObject> MakePlanDetails(int32 Index, const FString& LayerName, const FString& LayerInfoAssetPath)
{
    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetNumberField(TEXT("index"), Index);
    Details->SetStringField(TEXT("name"), LayerName);
    Details->SetStringField(TEXT("layer_info_asset_path"), LayerInfoAssetPath);
    return Details;
}

ULandscapeLayerInfoObject* CreateLayerInfoAsset(const FLandscapeLayerInfoPlan& Plan)
{
    UPackage* Package = CreatePackage(*Plan.LayerInfoPackageName);
    if (Package == nullptr)
    {
        return nullptr;
    }

    ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
        Package,
        ULandscapeLayerInfoObject::StaticClass(),
        FName(*Plan.LayerInfoAssetName),
        RF_Public | RF_Standalone | RF_Transactional);
    if (LayerInfo == nullptr)
    {
        return nullptr;
    }

    LayerInfo->LayerName = Plan.LayerName;
    LayerInfo->LayerUsageDebugColor = LayerInfo->GenerateLayerUsageDebugColor();
#if WITH_EDITORONLY_DATA
    if (Plan.bSetNoWeightBlend)
    {
        LayerInfo->bNoWeightBlend = Plan.bNoWeightBlend;
    }
#endif
    FAssetRegistryModule::AssetCreated(LayerInfo);
    Package->MarkPackageDirty();
    LayerInfo->PostEditChange();
    return LayerInfo;
}
}

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message)
{
    return MakeOperationError(Operation, RequestId, Code, Message);
}

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message,
    const TSharedPtr<FJsonObject>& Details)
{
    return MakeOperationError(Operation, RequestId, Code, Message, Details);
}

ALandscapeProxy* ResolveLandscapeProxyForLayerInfo(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutError)
{
    FString ActorPath;
    if (!Payload->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
    {
        OutError = MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("invalid_request"), TEXT("actor_path is required"));
        return nullptr;
    }

    ALandscapeProxy* Landscape = Cast<ALandscapeProxy>(ResolveObjectByPath(ActorPath));
    if (Landscape == nullptr)
    {
        OutError = MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("landscape_not_found"), TEXT("Landscape actor could not be resolved"));
        return nullptr;
    }
    return Landscape;
}

bool BuildLandscapeLayerInfoPlan(
    ALandscapeProxy* Landscape,
    const TSharedPtr<FJsonObject>& LayerPayload,
    int32 Index,
    const FString& Operation,
    const FString& RequestId,
    FLandscapeLayerInfoPlan& OutPlan,
    TSharedPtr<FJsonObject>& OutError)
{
    FString LayerNameText;
    FString LayerInfoAssetPath;
    if (
        !LayerPayload->TryGetStringField(TEXT("name"), LayerNameText)
        || LayerNameText.IsEmpty()
        || !LayerPayload->TryGetStringField(TEXT("layer_info_asset_path"), LayerInfoAssetPath)
        || LayerInfoAssetPath.IsEmpty())
    {
        OutError = MakeLandscapeLayerInfoError(Operation, RequestId, TEXT("invalid_layer_item"), TEXT("each layers item requires name and layer_info_asset_path"));
        return false;
    }

    FString PackageName;
    FString AssetName;
    FText Reason;
    if (!ParseAssetPath(LayerInfoAssetPath, PackageName, AssetName, Reason))
    {
        OutError = MakeLandscapeLayerInfoError(
            Operation,
            RequestId,
            TEXT("invalid_layer_info_asset_path"),
            Reason.ToString(),
            MakePlanDetails(Index, LayerNameText, LayerInfoAssetPath));
        return false;
    }

    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    ULandscapeLayerInfoObject* ExistingLayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *ObjectPath);
    UObject* ExistingObject = ExistingLayerInfo != nullptr ? ExistingLayerInfo : LoadObject<UObject>(nullptr, *ObjectPath);
    if (ExistingObject != nullptr && ExistingLayerInfo == nullptr)
    {
        OutError = MakeLandscapeLayerInfoError(
            Operation,
            RequestId,
            TEXT("layer_info_asset_wrong_class"),
            TEXT("layer_info_asset_path resolves to a non-LandscapeLayerInfoObject asset"),
            MakePlanDetails(Index, LayerNameText, ObjectPath));
        return false;
    }

    bool bCreateIfMissing = false;
    LayerPayload->TryGetBoolField(TEXT("create_if_missing"), bCreateIfMissing);
    if (ExistingLayerInfo == nullptr && !bCreateIfMissing)
    {
        OutError = MakeLandscapeLayerInfoError(
            Operation,
            RequestId,
            TEXT("layer_info_asset_not_found"),
            TEXT("LayerInfo asset could not be loaded and create_if_missing is false"),
            MakePlanDetails(Index, LayerNameText, ObjectPath));
        return false;
    }

    const FName LayerName(*LayerNameText);
    ALandscapeProxy* TargetLayerOwner = TargetLayerOwnerFor(Landscape);
    const FLandscapeTargetLayerSettings* ExistingTargetLayer = TargetLayerOwner != nullptr ? TargetLayerOwner->GetTargetLayers().Find(LayerName) : nullptr;
    const FString ExistingLayerInfoLayerName = ExistingLayerInfo != nullptr ? ExistingLayerInfo->LayerName.ToString() : FString();
    if (ExistingLayerInfo != nullptr && ExistingLayerInfo->LayerName != NAME_None && ExistingLayerInfo->LayerName != LayerName)
    {
        TSharedPtr<FJsonObject> Details = MakePlanDetails(Index, LayerNameText, ObjectPath);
        Details->SetStringField(TEXT("existing_layer_name"), ExistingLayerInfoLayerName);
        OutError = MakeLandscapeLayerInfoError(
            Operation,
            RequestId,
            TEXT("layer_info_name_mismatch"),
            TEXT("Existing LayerInfo has a different LayerName; use a matching LayerInfo asset or a new asset path"),
            Details);
        return false;
    }

    OutPlan.LayerName = LayerName;
    OutPlan.LayerInfoObjectPath = ObjectPath;
    OutPlan.LayerInfoPackageName = PackageName;
    OutPlan.LayerInfoAssetName = AssetName;
    OutPlan.ExistingLayerInfo = ExistingLayerInfo;
    OutPlan.ExistingLayerInfoLayerName = ExistingLayerInfoLayerName;
    OutPlan.bLayerInfoExists = ExistingLayerInfo != nullptr;
    OutPlan.bTargetLayerExists = ExistingTargetLayer != nullptr;
    OutPlan.OldLayerInfoPath = ExistingTargetLayer != nullptr ? LayerInfoPath(ExistingTargetLayer->LayerInfoObj) : FString();
    OutPlan.bCreateIfMissing = bCreateIfMissing;
    OutPlan.bSetNoWeightBlend = LayerPayload->HasField(TEXT("no_weight_blend"));
    LayerPayload->TryGetBoolField(TEXT("no_weight_blend"), OutPlan.bNoWeightBlend);
    return true;
}

bool ApplyLandscapeLayerInfoPlan(
    ALandscapeProxy* Landscape,
    FLandscapeLayerInfoPlan& Plan,
    TArray<UPackage*>& OutPackagesToSave)
{
    ALandscapeProxy* TargetLayerOwner = TargetLayerOwnerFor(Landscape);
    if (TargetLayerOwner == nullptr)
    {
        return false;
    }

    ULandscapeLayerInfoObject* LayerInfo = Plan.ExistingLayerInfo;
    if (LayerInfo == nullptr)
    {
        LayerInfo = CreateLayerInfoAsset(Plan);
        if (LayerInfo == nullptr)
        {
            return false;
        }
        Plan.bLayerInfoCreated = true;
        OutPackagesToSave.AddUnique(LayerInfo->GetOutermost());
    }
    else
    {
        bool bLayerInfoChanged = false;
        if (LayerInfo->LayerName == NAME_None)
        {
            LayerInfo->Modify();
            LayerInfo->LayerName = Plan.LayerName;
            bLayerInfoChanged = true;
        }
#if WITH_EDITORONLY_DATA
        if (Plan.bSetNoWeightBlend && LayerInfo->bNoWeightBlend != Plan.bNoWeightBlend)
        {
            LayerInfo->Modify();
            LayerInfo->bNoWeightBlend = Plan.bNoWeightBlend;
            bLayerInfoChanged = true;
        }
#endif
        if (bLayerInfoChanged)
        {
            LayerInfo->MarkPackageDirty();
            LayerInfo->PostEditChange();
            OutPackagesToSave.AddUnique(LayerInfo->GetOutermost());
        }
    }

    if (TargetLayerOwner->HasTargetLayer(Plan.LayerName))
    {
        TargetLayerOwner->UpdateTargetLayer(Plan.LayerName, FLandscapeTargetLayerSettings(LayerInfo));
    }
    else
    {
        TargetLayerOwner->AddTargetLayer(Plan.LayerName, FLandscapeTargetLayerSettings(LayerInfo));
    }

    if (ULandscapeInfo* LandscapeInfo = TargetLayerOwner->GetLandscapeInfo())
    {
        LandscapeInfo->Modify();
        LandscapeInfo->CreateTargetLayerSettingsFor(LayerInfo);
        LandscapeInfo->UpdateLayerInfoMap(TargetLayerOwner, true);
    }

    TargetLayerOwner->MarkPackageDirty();
    OutPackagesToSave.AddUnique(TargetLayerOwner->GetOutermost());
    return true;
}

void RefreshLandscapeLayerInfoEditorState(ALandscapeProxy* Landscape)
{
    ALandscapeProxy* TargetLayerOwner = TargetLayerOwnerFor(Landscape);
    if (TargetLayerOwner == nullptr)
    {
        return;
    }

    if (ULandscapeInfo* LandscapeInfo = TargetLayerOwner->GetLandscapeInfo())
    {
        LandscapeInfo->UpdateLayerInfoMap(TargetLayerOwner, true);
    }

    if (GEditor != nullptr)
    {
        GEditor->RedrawLevelEditingViewports();
    }

    if (GLevelEditorModeToolsIsValid())
    {
        FEditorModeTools& ModeTools = GLevelEditorModeTools();
        if (ModeTools.IsModeActive(FBuiltinEditorModes::EM_Landscape))
        {
            ModeTools.DeactivateMode(FBuiltinEditorModes::EM_Landscape);
            ModeTools.ActivateMode(FBuiltinEditorModes::EM_Landscape);
        }
    }
}
}
