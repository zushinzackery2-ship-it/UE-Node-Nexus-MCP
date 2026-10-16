#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UBlueprint;
class UClass;

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;
bool ValidateDelegatePlanBinding(UBlueprint* Blueprint, UClass* NodeClass,
    const TSharedPtr<FJsonObject>& Config, const FApplyContext& Context, FString& OutError);
}
