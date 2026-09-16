#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

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

// Parse/admit before enqueueing; the parsed payload crosses to the game thread once.
bool PrepareBridgeRequest(const FString& Body, TSharedPtr<FJsonObject>& Request, FString& Error, uint32 Peer = 0, bool bCommandlet = false);
FString DispatchParsedRequest(const TSharedPtr<FJsonObject>& Request);
// Dedicated local commandlet entry point; network requests use PrepareBridgeRequest.
FString DispatchBodyToResponseString(const FString& BodyString);
bool IsBridgeRequestActive();
const FString& ActiveBridgeRequestId();
}
