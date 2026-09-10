#include "UeNodeNexusBridgeRequestDispatch.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "HAL/PlatformTime.h"

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

FString DispatchBodyToResponseString(const FString& BodyString)
{
    TSharedPtr<FJsonObject> RequestJson;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);

    if (!FJsonSerializer::Deserialize(Reader, RequestJson) || !RequestJson.IsValid())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(TEXT("unknown"), TEXT(""), false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_json"), TEXT("Request body is not valid JSON")));
        return SerializeJsonObjectToString(Response);
    }

    FString Operation;
    FString RequestId;
    TSharedPtr<FJsonObject> Payload;
    if (!RequestJson->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty() ||
        !RequestJson->TryGetStringField(TEXT("request_id"), RequestId) ||
        !TryGetPayload(RequestJson, Payload))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation.IsEmpty() ? TEXT("unknown") : Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_envelope"), TEXT("Request must include operation, request_id, and payload object")));
        return SerializeJsonObjectToString(Response);
    }

    // Requests run as game-thread tasks. If a handler ever pumps messages (modal dialog,
    // slow task, shader-compile wait), the task graph can start the next request *inside*
    // it; nested graph edits during a compile cancellation have crashed the editor.
    // Refuse instead of nesting; the client retries.
    if (GRequestActive)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("bridge_busy"), TEXT("another bridge request is still executing on the game thread; retry shortly")));
        return SerializeJsonObjectToString(Response);
    }
    FBridgeWorkScope Scope(RequestId);
    const double Started = FPlatformTime::Seconds();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=begin"), *RequestId, *Operation);
    TSharedPtr<FJsonObject> Response = DispatchOperation(Operation, RequestId, Payload);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s operation=%s phase=end duration_ms=%.3f"),
        *RequestId, *Operation, (FPlatformTime::Seconds() - Started) * 1000.0);
    return SerializeJsonObjectToString(Response);
}
}
