#pragma once

#include "CoreMinimal.h"

class AActor;
class UActorComponent;
class UNexusSceneData;

namespace UeNodeNexusBridge::Scene
{
struct FApply;
FGuid StableGuid(const FString& Text);
FGuid ComponentId(UActorComponent* Component);
FString OwnerKey(AActor* Actor);
UNexusSceneData* EnsureMetadata(AActor* Actor);
bool BindComponent(UActorComponent* Component, const FGuid& Id);
void ForgetComponent(UActorComponent* Component);
bool BindScene(FApply& Context);
bool BindAppliedIdentities(FApply& Context);
}
