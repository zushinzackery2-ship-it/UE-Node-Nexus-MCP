#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle::SlateDialogs
{
void Install();
void Uninstall();
// Call while holding the guard-state mutex; Slate itself is read on its UI thread.
TSharedPtr<FJsonObject> OpenLocked();
}
