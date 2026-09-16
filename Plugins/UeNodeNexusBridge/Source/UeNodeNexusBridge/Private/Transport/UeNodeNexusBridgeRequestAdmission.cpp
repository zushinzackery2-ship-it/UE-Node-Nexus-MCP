#include "UeNodeNexusBridgeRequestDispatch.h"

#include "NexusLifecycle.h"
#include "UeNodeNexusBridgeJson.h"
#include "Serialization/JsonSerializer.h"

namespace UeNodeNexusBridge
{
bool PrepareBridgeRequest(const FString& Body, TSharedPtr<FJsonObject>& Request, FString& Error, uint32 Peer, bool bCommandlet)
{
    FString Operation, RequestId, Code;
    TSharedPtr<FJsonObject> Payload;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Request) || !Request.IsValid())
    {
        Code = TEXT("invalid_json");
    }
    else if (!Request->HasTypedField<EJson::String>(TEXT("operation"))
        || !Request->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty()
        || !Request->HasTypedField<EJson::String>(TEXT("request_id"))
        || !Request->TryGetStringField(TEXT("request_id"), RequestId) || RequestId.IsEmpty()
        || !TryGetPayload(Request, Payload))
    {
        Code = TEXT("invalid_envelope");
    }
    else if (bCommandlet && IsRunningCommandlet())
    {
        return true;
    }
    else if (NexusLifecycle::Admit(Request, Peer, Code))
    {
        return true;
    }
    auto Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Code, TEXT("Request was rejected before execution; obtain a current Broker work scope")));
    Error = SerializeJsonObjectToString(Response);
    return false;
}

FString DispatchBodyToResponseString(const FString& Body)
{
    TSharedPtr<FJsonObject> Request;
    FString Error;
    return PrepareBridgeRequest(Body, Request, Error, 0, true) ? DispatchParsedRequest(Request) : Error;
}
}
