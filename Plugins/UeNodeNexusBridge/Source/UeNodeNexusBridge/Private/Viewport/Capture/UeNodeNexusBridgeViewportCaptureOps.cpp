#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeJson.h"
#include "Viewport/Capture/NexusCapture.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleViewportCapture(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    Capture::FRequest Request;
    FString Message;
    if (const TSharedPtr<FJsonObject> Violation = Capture::ReadRequest(*Payload, Request, Message))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), Message, Violation);
    }
    FString Error;
    auto Data = Capture::Submit(RequestId, Request.Name, Request.Target, Request.bShowUi, Request.Timeout, Request.bDryRun, Error);
    if (!Data.IsValid())
    {
        return MakeOperationError(Operation, RequestId, Error, TEXT("viewport capture was not admitted"));
    }
    auto Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
