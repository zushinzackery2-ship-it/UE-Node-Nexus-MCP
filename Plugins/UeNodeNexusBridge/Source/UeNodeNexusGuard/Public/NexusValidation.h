#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle::Validation
{
UENODENEXUSGUARD_API void Install();
UENODENEXUSGUARD_API void Uninstall();
UENODENEXUSGUARD_API TSharedPtr<FJsonObject> Environment();
UENODENEXUSGUARD_API TSharedPtr<FJsonObject> BootstrapStatus();
}
