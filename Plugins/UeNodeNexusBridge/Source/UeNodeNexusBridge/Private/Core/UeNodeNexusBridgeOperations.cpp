#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperationRegistry.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> UnsupportedOperation(const FString& Operation, const FString& RequestId)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(TEXT("operation_not_implemented"), TEXT("Operation is known by the MCP contract but not implemented in this bridge build")));
    return Response;
}

TSharedPtr<FJsonObject> HandleDiagnosticsGet(const FString& Operation, const FString& RequestId)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>());
    Data->SetStringField(TEXT("source"), TEXT("UeNodeNexusBridge"));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> DispatchOperation(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    // Core, AutoIndex, and plugin-contributed operations all dispatch through
    // the shared registry. The only fallback left is the machine-readable
    // unsupported-operation envelope.
    TSharedPtr<FJsonObject> RegisteredResponse;
    if (DispatchRegisteredOperation(Operation, RequestId, Payload, RegisteredResponse))
    {
        return RegisteredResponse;
    }

    return UnsupportedOperation(Operation, RequestId);
}
}
