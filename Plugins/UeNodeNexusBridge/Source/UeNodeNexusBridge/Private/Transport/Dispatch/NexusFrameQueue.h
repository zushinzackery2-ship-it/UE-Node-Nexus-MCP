#pragma once

#include "Async/Future.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge
{
void StartRequestQueue();
void StopRequestQueue();
void PumpRequestQueue();
TFuture<FString> EnqueueBridgeRequest(const TSharedPtr<FJsonObject>& Request);
}
