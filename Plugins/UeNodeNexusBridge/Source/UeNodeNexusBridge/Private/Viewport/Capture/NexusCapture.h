#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FViewport;

namespace UeNodeNexusBridge::Capture
{
inline constexpr int32 MaxScreenshotNameLength = 100;
inline constexpr double MinCaptureTimeout = 1;
inline constexpr double MaxCaptureTimeout = 120;

// viewport_capture's accepted payload; members hold the defaults of omitted fields.
struct FRequest
{
    FString Name = TEXT("UeNodeNexus");
    FString Target = TEXT("level");
    double Timeout = 30;
    bool bShowUi = false;
    bool bDryRun = false;
};

FViewport* ResolveViewport(const FString& Target, FString& Resolved);
// ASCII letters, digits, '_' and '-', 1..100 characters: a name without directory or extension.
bool IsSafeScreenshotName(const FString& Name);
// Each check returns null when the input is admissible; otherwise error details whose
// "field" names the first violated field, with Message describing its rule.
TSharedPtr<FJsonObject> CheckNameAndTimeout(const FString& Name, double Timeout, FString& Message);
TSharedPtr<FJsonObject> ReadRequest(const FJsonObject& Payload, FRequest& Request, FString& Message);
TSharedPtr<FJsonObject> Submit(const FString& RequestId, const FString& BaseName,
    const FString& Target, bool bShowUi, double Timeout, bool bDryRun, FString& Error, bool bRetainScope = false,
    const TSharedPtr<FJsonObject>& Phase = nullptr);
void Stop();
bool WriteReceipt(const TSharedPtr<FJsonObject>& Receipt);
bool VerifyImage(const FString& Path, int32& Width, int32& Height, FString* Digest = nullptr);
}
