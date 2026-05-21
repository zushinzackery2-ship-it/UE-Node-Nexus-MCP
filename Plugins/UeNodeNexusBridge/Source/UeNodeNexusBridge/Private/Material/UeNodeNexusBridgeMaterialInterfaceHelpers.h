#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeMaterialInterfaceError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message);
UMaterialInterface* ResolveMaterialInterfaceFromPayload(const TSharedPtr<FJsonObject>& Payload);
UMaterialInterface* ResolveComponentSlotMaterial(const TSharedPtr<FJsonObject>& Payload, UMeshComponent*& OutComponent, int32& OutSlotIndex);
TSharedPtr<FJsonObject> BuildMaterialResolveData(UMaterialInterface* MaterialInterface, bool bIncludeParams);
bool MaterialMatchesQuery(UMaterialInterface* Candidate, UMaterialInterface* Query);
void ApplyDynamicMaterialParam(UMaterialInstanceDynamic* Mid, const TSharedPtr<FJsonObject>& Param, bool& bApplied);
}
