#pragma once

#include "CoreMinimal.h"

namespace UeNodeNexusBridge
{
class FBridgeWorkScope
{
public:
    explicit FBridgeWorkScope(const FString& RequestId);
    ~FBridgeWorkScope();
    FBridgeWorkScope(const FBridgeWorkScope&) = delete;
    FBridgeWorkScope& operator=(const FBridgeWorkScope&) = delete;
};

// Transport-neutral request handler. Parses one JSON envelope body string,
// validates the {operation, request_id, payload} contract, dispatches the
// operation, and returns the serialized JSON response string.
//
// MUST be invoked on the game thread: DispatchOperation touches UObjects and
// editor subsystems. Transports (named pipe) are responsible for marshaling a
// raw body string onto the game thread and ferrying the returned string back.
FString DispatchBodyToResponseString(const FString& BodyString);
bool IsBridgeRequestActive();
const FString& ActiveBridgeRequestId();
}
