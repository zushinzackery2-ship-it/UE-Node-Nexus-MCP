#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UeNodeNexusBridge::AssetDeletion
{
struct FDeleteTarget
{
    FString AssetPath;
    FString PackageName;
    bool bRemovedFromMemory = false;
};

TArray<TSharedPtr<FJsonValue>> PathValues(const TArray<FString>& Paths);
TSharedPtr<FJsonObject> Result(const FString& Operation, const FString& RequestId,
    const TArray<FDeleteTarget>& Targets, const TArray<FName>& External, int32 DeletedObjects,
    bool bCleanupAfterDelete, const TSharedPtr<FJsonObject>& Item);
}
