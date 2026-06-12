#include "UeNodeNexusBridgeLandscapeLayerInfoOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
TSharedPtr<FJsonObject> PlanToJson(const FLandscapeLayerInfoPlan& Plan, bool bApplied)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Plan.LayerName.ToString());
    Item->SetStringField(TEXT("old_layer_info_asset_path"), Plan.OldLayerInfoPath);
    Item->SetStringField(TEXT("new_layer_info_asset_path"), Plan.LayerInfoObjectPath);
    Item->SetStringField(TEXT("existing_layer_info_layer_name"), Plan.ExistingLayerInfoLayerName);
    Item->SetBoolField(TEXT("target_layer_existed"), Plan.bTargetLayerExists);
    Item->SetBoolField(TEXT("layer_info_existed"), Plan.bLayerInfoExists);
    Item->SetBoolField(TEXT("layer_info_created"), Plan.bLayerInfoCreated);
    Item->SetBoolField(TEXT("applied"), bApplied);
    Item->SetBoolField(TEXT("changed"), Plan.OldLayerInfoPath != Plan.LayerInfoObjectPath || !Plan.bLayerInfoExists);
    return Item;
}
}

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoDiff(const TArray<FLandscapeLayerInfoPlan>& Plans, bool bApplied)
{
    TArray<TSharedPtr<FJsonValue>> ChangedLayers;
    TArray<TSharedPtr<FJsonValue>> CreatedLayerInfos;
    for (const FLandscapeLayerInfoPlan& Plan : Plans)
    {
        TSharedPtr<FJsonObject> Item = PlanToJson(Plan, bApplied);
        ChangedLayers.Add(MakeShared<FJsonValueObject>(Item));
        if (Plan.bLayerInfoCreated || (!bApplied && !Plan.bLayerInfoExists && Plan.bCreateIfMissing))
        {
            CreatedLayerInfos.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    Diff->SetArrayField(TEXT("landscape_layers_changed"), ChangedLayers);
    Diff->SetArrayField(TEXT("layer_infos_created"), CreatedLayerInfos);
    return Diff;
}

TSharedPtr<FJsonObject> MakeLandscapeLayerInfoSavedPackagesJson(const TArray<UPackage*>& Packages, bool bSaved)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (UPackage* Package : Packages)
    {
        if (Package == nullptr)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("package_name"), Package->GetName());
        Item->SetBoolField(TEXT("package_dirty"), Package->IsDirty());
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("packages"), Items);
    return Result;
}

bool AnyLandscapeLayerInfoPlanChanges(const TArray<FLandscapeLayerInfoPlan>& Plans)
{
    for (const FLandscapeLayerInfoPlan& Plan : Plans)
    {
        if (Plan.OldLayerInfoPath != Plan.LayerInfoObjectPath || !Plan.bLayerInfoExists)
        {
            return true;
        }
    }
    return false;
}
}
