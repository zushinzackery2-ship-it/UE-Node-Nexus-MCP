#include "UeNodeNexusBridgeLandscapeLayerInfoOps.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "UeNodeNexusBridgeLandscapeLayerInfoShared.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
ALandscapeProxy* ResolveLandscapeTargetLayerOwner(ALandscapeProxy* Landscape)
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

static ULandscapeLayerInfoObject* CreateLayerInfoAsset(
    const FLandscapeLayerInfoPlan& Plan)
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

bool ApplyLandscapeLayerInfoPlan(
    ALandscapeProxy* Landscape,
    FLandscapeLayerInfoPlan& Plan,
    TArray<UPackage*>& OutPackagesToSave)
{
    ALandscapeProxy* TargetLayerOwner = ResolveLandscapeTargetLayerOwner(Landscape);
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
        TargetLayerOwner->UpdateTargetLayer(
            Plan.LayerName,
            FLandscapeTargetLayerSettings(LayerInfo));
    }
    else
    {
        TargetLayerOwner->AddTargetLayer(
            Plan.LayerName,
            FLandscapeTargetLayerSettings(LayerInfo));
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
    ALandscapeProxy* TargetLayerOwner = ResolveLandscapeTargetLayerOwner(Landscape);
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
