#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
void Start();
void Stop();
void BeginSession(bool bPie);
void EndSession();
void SetCaptureReady(bool bReady);
void Record(const TSharedPtr<FJsonObject>& Event);
bool ValidateRequest(const TSharedPtr<FJsonObject>& Payload, FString& Error);
TSharedPtr<FJsonObject> Read(const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> Snapshot();
void StartMessages();
void StopMessages();
}
