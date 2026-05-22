#include "UeNodeNexusBridgeOperationRegistry.h"

#include "Containers/Map.h"

namespace UeNodeNexusBridge
{
namespace
{
TMap<FString, FBridgeOperationHandler>& Registry()
{
    static TMap<FString, FBridgeOperationHandler> Handlers;
    return Handlers;
}
}

bool RegisterOperationHandler(const FString& Operation, FBridgeOperationHandler Handler)
{
    if (Operation.IsEmpty() || !Handler)
    {
        return false;
    }

    Registry().Add(Operation, MoveTemp(Handler));
    return true;
}

void UnregisterOperationHandler(const FString& Operation)
{
    Registry().Remove(Operation);
}

bool DispatchRegisteredOperation(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FJsonObject>& OutResponse)
{
    const FBridgeOperationHandler* Handler = Registry().Find(Operation);
    if (Handler == nullptr)
    {
        return false;
    }

    OutResponse = (*Handler)(Operation, RequestId, Payload);
    return true;
}

bool IsOperationRegistered(const FString& Operation)
{
    return Registry().Contains(Operation);
}
}
