#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FViewport;

namespace UeNodeNexusBridge::Capture
{
FViewport* ResolveViewport(const FString& Target, FString& Resolved);
bool IsSafeScreenshotName(const FString& Name);
TSharedPtr<FJsonObject> Submit(const FString& RequestId, const FString& BaseName,
    const FString& Target, bool bShowUi, double Timeout, bool bDryRun, FString& Error, bool bRetainScope = false,
    const TSharedPtr<FJsonObject>& Phase = nullptr);
void Stop();
bool WriteReceipt(const TSharedPtr<FJsonObject>& Receipt);
bool VerifyImage(const FString& Path, int32& Width, int32& Height, FString* Digest = nullptr);
}
