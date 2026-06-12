#include "UeNodeNexusBridgeLandscapeGrassTypeOps.h"

#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString ScalingToString(EGrassScaling Scaling)
{
    switch (Scaling)
    {
    case EGrassScaling::Uniform: return TEXT("Uniform");
    case EGrassScaling::Free: return TEXT("Free");
    case EGrassScaling::LockXY: return TEXT("LockXY");
    default: return TEXT("Unknown");
    }
}

TSharedPtr<FJsonObject> IntervalToJson(const FFloatInterval& Interval)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("min"), Interval.Min);
    Json->SetNumberField(TEXT("max"), Interval.Max);
    return Json;
}

TSharedPtr<FJsonObject> PatchToJson(const FGrassVarietyPatch& Patch, bool bApplied)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetNumberField(TEXT("index"), Patch.Index);
    Item->SetStringField(TEXT("grass_mesh"), Patch.GrassMeshPath);
    Item->SetStringField(TEXT("old_scaling"), ScalingToString(Patch.OldScaling));
    Item->SetStringField(TEXT("new_scaling"), ScalingToString(Patch.NewScaling));
    Item->SetObjectField(TEXT("old_scale_x"), IntervalToJson(Patch.OldScaleX));
    Item->SetObjectField(TEXT("new_scale_x"), IntervalToJson(Patch.NewScaleX));
    Item->SetObjectField(TEXT("old_scale_y"), IntervalToJson(Patch.OldScaleY));
    Item->SetObjectField(TEXT("new_scale_y"), IntervalToJson(Patch.NewScaleY));
    Item->SetObjectField(TEXT("old_scale_z"), IntervalToJson(Patch.OldScaleZ));
    Item->SetObjectField(TEXT("new_scale_z"), IntervalToJson(Patch.NewScaleZ));
    Item->SetBoolField(TEXT("changed"), Patch.bChanged);
    Item->SetBoolField(TEXT("applied"), bApplied);
    return Item;
}
}

TSharedPtr<FJsonObject> MakeGrassTypeError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message,
    int32 Index)
{
    TSharedPtr<FJsonObject> Details;
    if (Index != INDEX_NONE)
    {
        Details = MakeShared<FJsonObject>();
        Details->SetNumberField(TEXT("index"), Index);
    }
    return MakeOperationError(Operation, RequestId, Code, Message, Details);
}

TSharedPtr<FJsonObject> MakeGrassTypeDiff(const TArray<FGrassVarietyPatch>& Patches, bool bApplied)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FGrassVarietyPatch& Patch : Patches)
    {
        Items.Add(MakeShared<FJsonValueObject>(PatchToJson(Patch, bApplied)));
    }

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    Diff->SetArrayField(TEXT("grass_varieties_changed"), Items);
    return Diff;
}

bool AnyGrassTypePatchChanged(const TArray<FGrassVarietyPatch>& Patches)
{
    for (const FGrassVarietyPatch& Patch : Patches)
    {
        if (Patch.bChanged)
        {
            return true;
        }
    }
    return false;
}
}
