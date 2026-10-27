#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UActorComponent;
class UMeshComponent;
class FJsonValue;

namespace UeNodeNexusBridge
{
bool TryGetComponentDefaultsObject(
    const TSharedPtr<FJsonObject>& Operation,
    const TSharedPtr<FJsonObject>*& OutDefaults);

bool ApplyMaterialDefault(
    UMeshComponent* MeshComponent,
    const TSharedPtr<FJsonValue>& Value,
    const TSharedPtr<FJsonObject>& Defaults,
    FString& OutError);

bool ApplyBlueprintComponentDefaults(
    UActorComponent* ComponentTemplate,
    const TSharedPtr<FJsonObject>& Defaults,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    const FString& ComponentName,
    FString& OutError);
}
