#pragma once

#include "CoreMinimal.h"
#include "UeNodeNexusBridgeListFormats.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
bool WantsRealIds(const TSharedPtr<FJsonObject>& Payload);
int32 ReadIndexedMaxNodes(const TSharedPtr<FJsonObject>& Payload);
}
