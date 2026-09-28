#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle::Dialogs
{
// bAcknowledge: nobody can answer an advisory prompt of this process (a managed
// launch, or a window that was started hidden or renders offscreen).
void Install(bool bAcknowledge);
void Uninstall();
// Rewrap whatever FCoreDelegates::ModalMessageDialog holds when it is not the wrapper.
void Wrap();
// With State().Mutex held.
void AppendStatus(const TSharedPtr<FJsonObject>& Status);
TSharedPtr<FJsonObject> OpenLocked();
}
