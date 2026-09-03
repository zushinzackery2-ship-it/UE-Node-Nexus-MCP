#include "UeNodeNexusBridgeRequestDispatch.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"

namespace UeNodeNexusBridge
{
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
    static int32 GDispatchDepth = 0;
    if (GDispatchDepth > 0)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("bridge_busy"), TEXT("another bridge request is still executing on the game thread; retry shortly")));
        return SerializeJsonObjectToString(Response);
    }
    ++GDispatchDepth;
    TSharedPtr<FJsonObject> Response = DispatchOperation(Operation, RequestId, Payload);
    --GDispatchDepth;
    return SerializeJsonObjectToString(Response);
}
}
