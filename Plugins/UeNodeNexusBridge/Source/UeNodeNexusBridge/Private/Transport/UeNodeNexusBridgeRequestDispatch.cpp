#include "UeNodeNexusBridgeRequestDispatch.h"
#include "RenderAssetUpdate.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "HAL/PlatformTime.h"
#include "NexusLifecycle.h"
#include "Safety/NexusAsyncWork.h"
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/GarbageCollection.h"

namespace UeNodeNexusBridge
{
static bool GRequestActive = false;
static FString GRequestId;

FBridgeWorkScope::FBridgeWorkScope(const FString& RequestId)
{
    check(IsInGameThread() && !GRequestActive);
    GRequestActive = true;
    GRequestId = RequestId;
}

FBridgeWorkScope::~FBridgeWorkScope()
{
    GRequestId.Reset();
    GRequestActive = false;
}

bool IsBridgeRequestActive()
{
    return GRequestActive || !Safety::AsyncBusyReason().IsEmpty();
}

bool IsBridgeDispatchActive()
{
    return GRequestActive;
}

const FString& ActiveBridgeRequestId()
{
    return GRequestId;
}

static FString RequestBusyReason(const FString& Operation, const TSharedPtr<FJsonObject>& Payload)
{
    if (GRequestActive)
    {
        return TEXT("request_active");
    }
    const FString EngineReason = Safety::EngineBusyReason();
    if (!EngineReason.IsEmpty())
    {
        return EngineReason;
    }
    // Capability discovery reads registry/build metadata, so a new caller can
    // learn the protocol while asynchronous resource ownership is retained.
    bool bInspectAssets = true;
    Payload->TryGetBoolField(TEXT("include_assets"), bInspectAssets);
    return Operation == TEXT("bridge_capabilities_get") || Operation == TEXT("runtime_smoke_status")
        || (Operation == TEXT("diagnostics_get") && !bInspectAssets) ? FString() : Safety::AsyncBusyReason();
}

FString DispatchParsedRequest(const TSharedPtr<FJsonObject>& RequestJson)
{
    const FString Operation = RequestJson->GetStringField(TEXT("operation"));
    const FString RequestId = RequestJson->GetStringField(TEXT("request_id"));
    TSharedPtr<FJsonObject> Payload;
    TryGetPayload(RequestJson, Payload);

    // The frame queue executes outside world ticking. Direct/internal dispatch
    // and engine callbacks still obey the same exclusion and lifetime checks.
    // Slate viewport resize suspends streaming and pumps game-thread tasks in
    // FlushRenderingCommands. Reject before any mutation; the owning frame must
    // unwind before a bridge operation may compile or save render assets.
    const FString BusyReason = RequestBusyReason(Operation, Payload);
    if (!BusyReason.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        auto Error = UeNodeNexusBridge::MakeError(FString(TEXT("bridge_busy")),
            TEXT("engine phase prevents bridge execution; retry after the owning frame resumes"));
        Error->SetStringField(TEXT("busy_reason"), BusyReason);
        Response->SetObjectField(TEXT("error"), Error);
        Response->SetObjectField(TEXT("runtime_diagnostics"), RuntimeDiagnostics::Snapshot());
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=deferred reason=%s"),
            *RequestId, *Operation, *BusyReason);
        NexusLifecycle::Complete(RequestId, false, TEXT("bridge_busy"), false);
        return SerializeJsonObjectToString(Response);
    }
    NexusLifecycle::Executing(RequestId);
    FBridgeWorkScope Scope(RequestId);
    const double Started = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=begin"), *RequestId, *Operation);
    TSharedPtr<FJsonObject> Response = DispatchOperation(Operation, RequestId, Payload);
    const TSharedPtr<FJsonObject>* Error = nullptr;
    FString Code;
    if (Response->TryGetObjectField(TEXT("error"), Error))
    {
        (*Error)->TryGetStringField(TEXT("code"), Code);
    }
    if (!Safety::Holds(RequestId))
    {
        NexusLifecycle::Complete(RequestId, Response->GetBoolField(TEXT("ok")), Code, Operation == TEXT("level_open"));
    }
    Response->SetNumberField(TEXT("context_epoch"), NexusLifecycle::Snapshot()->GetNumberField(TEXT("context_epoch")));
    Response->SetObjectField(TEXT("runtime_diagnostics"), RuntimeDiagnostics::Snapshot());
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=end duration_ms=%.3f"),
        *RequestId, *Operation, (FPlatformTime::Seconds() - Started) * 1000.0);
    return SerializeJsonObjectToString(Response);
}
}
