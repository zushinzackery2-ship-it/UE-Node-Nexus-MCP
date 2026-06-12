#pragma once

#include "CoreMinimal.h"
#include "LandscapeGrassType.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
struct FGrassVarietyPatch
{
    int32 Index = INDEX_NONE;
    FString GrassMeshPath;
    EGrassScaling OldScaling = EGrassScaling::Uniform;
    EGrassScaling NewScaling = EGrassScaling::Uniform;
    FFloatInterval OldScaleX;
    FFloatInterval OldScaleY;
    FFloatInterval OldScaleZ;
    FFloatInterval NewScaleX;
    FFloatInterval NewScaleY;
    FFloatInterval NewScaleZ;
    bool bChanged = false;
};

TSharedPtr<FJsonObject> MakeGrassTypeError(
    const FString& Operation,
    const FString& RequestId,
    const FString& Code,
    const FString& Message,
    int32 Index = INDEX_NONE);

bool BuildGrassVarietyPatch(
    ULandscapeGrassType* GrassType,
    const TSharedPtr<FJsonObject>& VarietyPayload,
    const FString& Operation,
    const FString& RequestId,
    FGrassVarietyPatch& OutPatch,
    TSharedPtr<FJsonObject>& OutError);

TSharedPtr<FJsonObject> MakeGrassTypeDiff(
    const TArray<FGrassVarietyPatch>& Patches,
    bool bApplied);

bool AnyGrassTypePatchChanged(const TArray<FGrassVarietyPatch>& Patches);
}
