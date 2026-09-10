#pragma once

#include "CoreMinimal.h"

class UInstancedStaticMeshComponent;
class UActorComponent;
class UNexusInstanceData;

namespace UeNodeNexusBridge::Instances
{
void StartupIdentity();
void ShutdownIdentity();
UNexusInstanceData* IdentityData(UInstancedStaticMeshComponent* Component);
bool IsIdentityBound(UInstancedStaticMeshComponent* Component);
bool IsConstructionOwned(UActorComponent* Component);
FString ContentRevision(UInstancedStaticMeshComponent* Component);
bool ReadIds(UInstancedStaticMeshComponent* Component, TArray<FGuid>& Out, FString& Error, bool bRebind = false);
UNexusInstanceData* BindIds(UInstancedStaticMeshComponent* Component, const TArray<FGuid>& Ids);
}
