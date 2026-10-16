#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlendProfile;
class USkeleton;

namespace UeNodeNexusBridge::Animation
{
    struct FMaskWeight
    {
        int32 BoneIndex = INDEX_NONE;
        float Weight = 0.0f;
    };

    bool ValidateMaskWeights(USkeleton* Skeleton, FName Name, const FString& Json,
        TArray<FMaskWeight>& Weights, FString& Error);
    TSharedPtr<FJsonObject> ReadMask(USkeleton* Skeleton, UBlendProfile* Profile);
    FString MaskResult(bool Success, const FString& Error,
        const TSharedPtr<FJsonObject>& Data = nullptr);
}
