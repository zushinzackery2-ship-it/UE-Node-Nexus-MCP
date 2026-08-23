#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UActorComponent;

namespace UeNodeNexusBridge
{
bool TryGetComponentDefaultsObject(
    const TSharedPtr<FJsonObject>& Operation,
    const TSharedPtr<FJsonObject>*& OutDefaults);

bool ApplyBlueprintComponentDefaults(
    UActorComponent* ComponentTemplate,
    const TSharedPtr<FJsonObject>& Defaults,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    const FString& ComponentName,
    FString& OutError);
}
