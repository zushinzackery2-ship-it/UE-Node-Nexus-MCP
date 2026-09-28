#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle::Startup
{
// PostConfigInit: record the launch, track phases and the latest log line, and
// wrap FCoreDelegates::ModalMessageDialog once the editor binds it.
void Install(bool bManaged);
void Uninstall();
void EnterPhase(const TCHAR* Phase);

// Status fields (``startup_progress``, ``waiting_for_user``, ``blocking_dialog``,
// ``dialog_notices``); called with State().Mutex held.
void AppendStatus(const TSharedPtr<FJsonObject>& Status);
TSharedPtr<FJsonObject> OpenDialogLocked();
}
