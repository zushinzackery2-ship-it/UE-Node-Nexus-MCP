#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterial;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildMaterialGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny);
TSharedPtr<FJsonObject> BuildMaterialFunctionGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny);
}
