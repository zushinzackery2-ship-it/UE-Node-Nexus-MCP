#include "UeNodeNexusBridgeRequestDispatch.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "HAL/PlatformTime.h"
#include "NexusLifecycle.h"

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
    return GRequestActive;
}

const FString& ActiveBridgeRequestId()
{
    return GRequestId;
}

FString DispatchParsedRequest(const TSharedPtr<FJsonObject>& RequestJson)
{
    const FString Operation = RequestJson->GetStringField(TEXT("operation"));
    const FString RequestId = RequestJson->GetStringField(TEXT("request_id"));
    TSharedPtr<FJsonObject> Payload;
    TryGetPayload(RequestJson, Payload);

    // Requests run as game-thread tasks. If a handler ever pumps messages (modal dialog,
    // slow task, shader-compile wait), the task graph can start the next request *inside*
    // it; nested graph edits during a compile cancellation have crashed the editor.
    // Refuse instead of nesting; the client retries.
    if (GRequestActive)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("bridge_busy"), TEXT("another bridge request is still executing on the game thread; retry shortly")));
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
    NexusLifecycle::Complete(RequestId, Response->GetBoolField(TEXT("ok")), Code, Operation == TEXT("level_open"));
    Response->SetNumberField(TEXT("context_epoch"), NexusLifecycle::Snapshot()->GetNumberField(TEXT("context_epoch")));
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=end duration_ms=%.3f"),
        *RequestId, *Operation, (FPlatformTime::Seconds() - Started) * 1000.0);
    return SerializeJsonObjectToString(Response);
}
}
