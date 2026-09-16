#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NexusLifecycle
{
using FSaveGuard = TFunction<bool()>;
using FInspector = TFunction<TSharedPtr<FJsonObject>(const TArray<FString>&, bool, const FSaveGuard&)>;

// Inspector is called only on the game thread; snapshots are immutable to readers.
UENODENEXUSGUARD_API void Attach(FInspector Inspector);
UENODENEXUSGUARD_API void Detach();
UENODENEXUSGUARD_API void Publish(const TSharedPtr<FJsonObject>& State);
UENODENEXUSGUARD_API TSharedPtr<FJsonObject> Snapshot();
UENODENEXUSGUARD_API TSharedPtr<FJsonObject> BuildIdentity();
UENODENEXUSGUARD_API bool Admit(const TSharedPtr<FJsonObject>& Request, uint32 Peer, FString& Error);
UENODENEXUSGUARD_API void Executing(const FString& RequestId);
UENODENEXUSGUARD_API void Complete(const FString& RequestId, bool bOk, const FString& Code, bool bContextChanged);
}
