#include "UeNodeNexusBridgeLandscapeGrassTypeOps.h"

#include "Engine/StaticMesh.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FGrassIntervalPatch
{
    bool bHasMin = false;
    bool bHasMax = false;
    float Min = 0.0f;
    float Max = 0.0f;
};

bool TryParseScaling(const FString& Text, EGrassScaling& OutScaling)
{
    FString Normalized = Text;
    Normalized.TrimStartAndEndInline();
    Normalized.ReplaceInline(TEXT("_"), TEXT(""));
    Normalized.ReplaceInline(TEXT(" "), TEXT(""));

    if (Normalized.Equals(TEXT("Uniform"), ESearchCase::IgnoreCase))
    {
        OutScaling = EGrassScaling::Uniform;
        return true;
    }
    if (Normalized.Equals(TEXT("Free"), ESearchCase::IgnoreCase))
    {
        OutScaling = EGrassScaling::Free;
        return true;
    }
    if (Normalized.Equals(TEXT("LockXY"), ESearchCase::IgnoreCase))
    {
        OutScaling = EGrassScaling::LockXY;
        return true;
    }
    return false;
}

bool ReadNumberIfPresent(
    const TSharedPtr<FJsonObject>& Object,
    const FString& Field,
    bool& bOutPresent,
    float& OutValue,
    FString& OutError)
{
    bOutPresent = false;
    if (!Object->HasField(Field))
    {
        return true;
    }

    double Number = 0.0;
    if (!Object->TryGetNumberField(Field, Number) || !FMath::IsFinite(Number) || Number < 0.0)
    {
        OutError = FString::Printf(TEXT("%s must be a finite number >= 0"), *Field);
        return false;
    }

    bOutPresent = true;
    OutValue = static_cast<float>(Number);
    return true;
}

bool ReadIntervalPatch(
    const TSharedPtr<FJsonObject>& VarietyPayload,
    const FString& Field,
    FGrassIntervalPatch& OutPatch,
    FString& OutError)
{
    const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
    if (!VarietyPayload->TryGetObjectField(Field, ObjectPtr) || ObjectPtr == nullptr || !ObjectPtr->IsValid())
    {
        if (VarietyPayload->HasField(Field))
        {
            OutError = FString::Printf(TEXT("%s must be an object with min and/or max"), *Field);
            return false;
        }
        return true;
    }

    if (
        !ReadNumberIfPresent(*ObjectPtr, TEXT("min"), OutPatch.bHasMin, OutPatch.Min, OutError)
        || !ReadNumberIfPresent(*ObjectPtr, TEXT("max"), OutPatch.bHasMax, OutPatch.Max, OutError))
    {
        OutError = Field + TEXT(".") + OutError;
        return false;
    }

    if (!OutPatch.bHasMin && !OutPatch.bHasMax)
    {
        OutError = FString::Printf(TEXT("%s requires min and/or max"), *Field);
        return false;
    }
    return true;
}

bool ApplyIntervalPatch(const FGrassIntervalPatch& Patch, FFloatInterval& Interval, FString& OutError)
{
    if (Patch.bHasMin)
    {
        Interval.Min = Patch.Min;
    }
    if (Patch.bHasMax)
    {
        Interval.Max = Patch.Max;
    }
    if (Interval.Min > Interval.Max)
    {
        OutError = TEXT("scale interval min cannot be greater than max");
        return false;
    }
    return true;
}

void CaptureCurrentVariety(const FGrassVariety& Variety, int32 Index, FGrassVarietyPatch& OutPatch)
{
    OutPatch.Index = Index;
    OutPatch.GrassMeshPath = Variety.GrassMesh != nullptr ? Variety.GrassMesh->GetPathName() : FString();
    OutPatch.OldScaling = Variety.Scaling;
    OutPatch.NewScaling = Variety.Scaling;
    OutPatch.OldScaleX = Variety.ScaleX;
    OutPatch.OldScaleY = Variety.ScaleY;
    OutPatch.OldScaleZ = Variety.ScaleZ;
    OutPatch.NewScaleX = Variety.ScaleX;
    OutPatch.NewScaleY = Variety.ScaleY;
    OutPatch.NewScaleZ = Variety.ScaleZ;
}

bool ApplyPayloadToPatch(
    const TSharedPtr<FJsonObject>& VarietyPayload,
    FGrassVarietyPatch& Patch,
    FString& OutError)
{
    double Multiplier = 1.0;
    if (VarietyPayload->HasField(TEXT("scale_multiplier")))
    {
        if (!VarietyPayload->TryGetNumberField(TEXT("scale_multiplier"), Multiplier) || !FMath::IsFinite(Multiplier) || Multiplier <= 0.0)
        {
            OutError = TEXT("scale_multiplier must be a finite number > 0");
            return false;
        }

        const float Scale = static_cast<float>(Multiplier);
        Patch.NewScaleX.Min *= Scale;
        Patch.NewScaleX.Max *= Scale;
        Patch.NewScaleY.Min *= Scale;
        Patch.NewScaleY.Max *= Scale;
        Patch.NewScaleZ.Min *= Scale;
        Patch.NewScaleZ.Max *= Scale;
    }

    FString ScalingText;
    if (VarietyPayload->TryGetStringField(TEXT("scaling"), ScalingText) && !TryParseScaling(ScalingText, Patch.NewScaling))
    {
        OutError = TEXT("scaling must be Uniform, Free, or LockXY");
        return false;
    }

    FGrassIntervalPatch ScaleX;
    FGrassIntervalPatch ScaleY;
    FGrassIntervalPatch ScaleZ;
    return ReadIntervalPatch(VarietyPayload, TEXT("scale_x"), ScaleX, OutError)
        && ApplyIntervalPatch(ScaleX, Patch.NewScaleX, OutError)
        && ReadIntervalPatch(VarietyPayload, TEXT("scale_y"), ScaleY, OutError)
        && ApplyIntervalPatch(ScaleY, Patch.NewScaleY, OutError)
        && ReadIntervalPatch(VarietyPayload, TEXT("scale_z"), ScaleZ, OutError)
        && ApplyIntervalPatch(ScaleZ, Patch.NewScaleZ, OutError);
}

bool HasPatchChanged(const FGrassVarietyPatch& Patch)
{
    return Patch.OldScaling != Patch.NewScaling
        || Patch.OldScaleX.Min != Patch.NewScaleX.Min
        || Patch.OldScaleX.Max != Patch.NewScaleX.Max
        || Patch.OldScaleY.Min != Patch.NewScaleY.Min
        || Patch.OldScaleY.Max != Patch.NewScaleY.Max
        || Patch.OldScaleZ.Min != Patch.NewScaleZ.Min
        || Patch.OldScaleZ.Max != Patch.NewScaleZ.Max;
}
}

bool BuildGrassVarietyPatch(
    ULandscapeGrassType* GrassType,
    const TSharedPtr<FJsonObject>& VarietyPayload,
    const FString& Operation,
    const FString& RequestId,
    FGrassVarietyPatch& OutPatch,
    TSharedPtr<FJsonObject>& OutError)
{
    double IndexNumber = 0.0;
    if (!VarietyPayload->TryGetNumberField(TEXT("index"), IndexNumber) || FMath::FloorToDouble(IndexNumber) != IndexNumber)
    {
        OutError = MakeGrassTypeError(Operation, RequestId, TEXT("invalid_variety_item"), TEXT("each varieties item requires integer index"));
        return false;
    }

    const int32 Index = static_cast<int32>(IndexNumber);
    if (!GrassType->GrassVarieties.IsValidIndex(Index))
    {
        OutError = MakeGrassTypeError(Operation, RequestId, TEXT("variety_index_out_of_range"), TEXT("variety index is outside GrassVarieties"), Index);
        return false;
    }

    CaptureCurrentVariety(GrassType->GrassVarieties[Index], Index, OutPatch);

    FString ErrorText;
    if (!ApplyPayloadToPatch(VarietyPayload, OutPatch, ErrorText))
    {
        OutError = MakeGrassTypeError(Operation, RequestId, TEXT("invalid_scale_request"), ErrorText, Index);
        return false;
    }

    OutPatch.bChanged = HasPatchChanged(OutPatch);
    return true;
}
}
