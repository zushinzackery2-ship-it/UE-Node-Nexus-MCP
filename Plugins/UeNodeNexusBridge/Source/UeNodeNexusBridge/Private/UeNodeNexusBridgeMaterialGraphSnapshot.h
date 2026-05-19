#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterial;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> BuildMaterialGraphSnapshot(const FString& Operation, const FString& RequestId, UMaterial* Material, bool bIncludeNodeParams, bool bIncludeLinks, bool bCompact, bool bWire, bool bWireMin, bool bWireTiny);
}
