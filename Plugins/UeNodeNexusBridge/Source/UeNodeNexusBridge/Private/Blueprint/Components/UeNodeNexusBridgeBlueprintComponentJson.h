#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UBlueprintGeneratedClass;

namespace UeNodeNexusBridge
{
// Collects a Blueprint's components for blueprint_details_get: SCS nodes on the
// generated class, optionally inherited SCS nodes from ancestor Blueprints, and
// native UActorComponent CDO properties. Emits compact rows or full objects and
// dedupes by component name. Each component carries an origin (empty = own SCS,
// ancestor class path = inherited).
void CollectBlueprintComponents(
    UBlueprintGeneratedClass* GeneratedClass,
    UObject* CDO,
    bool bCompact,
    bool bIncludeInherited,
    TArray<TSharedPtr<FJsonValue>>& OutComponents);
}
