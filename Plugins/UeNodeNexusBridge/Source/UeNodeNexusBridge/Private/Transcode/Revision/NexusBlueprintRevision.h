#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge::Collaboration
{
TSharedPtr<FJsonObject> BlueprintRevisionState(const TSharedPtr<FJsonObject>& Raw);
}
