#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
using FBridgeOperationHandler = TFunction<TSharedPtr<FJsonObject>(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload)>;

UENODENEXUSBRIDGE_API bool RegisterOperationHandler(const FString& Operation, FBridgeOperationHandler Handler);
UENODENEXUSBRIDGE_API void UnregisterOperationHandler(const FString& Operation);
UENODENEXUSBRIDGE_API bool DispatchRegisteredOperation(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    TSharedPtr<FJsonObject>& OutResponse);
UENODENEXUSBRIDGE_API bool IsOperationRegistered(const FString& Operation);
}
