#pragma once

#include "CoreMinimal.h"

namespace UeNodeNexusBridge::AssetDeletion
{
struct FClosureMember
{
    FString AssetPath;
    FName PackageName;
};

TArray<int32> DependentsFirst(const TArray<FClosureMember>& Members);
}
