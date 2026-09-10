#pragma once

#include "CoreMinimal.h"
#include "InstancedStaticMeshDelegates.h"

namespace UeNodeNexusBridge::Instances
{
bool ApplyIndexUpdates(TArray<FGuid>& Ids,
    TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> Updates, int32 NewCount);
}
