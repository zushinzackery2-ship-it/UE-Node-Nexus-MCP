#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeJson.h"
#include "Viewport/Capture/NexusCapture.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleViewportCapture(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Name = TEXT("UeNodeNexus");
    FString Target = TEXT("level");
    Payload->TryGetStringField(TEXT("filename"), Name);
    Payload->TryGetStringField(TEXT("target"), Target);
    bool bDryRun = false, bShowUi = false;
    double Timeout = 30;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("show_ui"), bShowUi);
    Payload->TryGetNumberField(TEXT("timeout_seconds"), Timeout);
    if (!Capture::IsSafeScreenshotName(Name) || (Target != TEXT("level") && Target != TEXT("active") && Target != TEXT("pie"))
        || !FMath::IsFinite(Timeout) || Timeout < 1 || Timeout > 120)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"),
            TEXT("filename must be a basename, target must be level/active/pie, timeout_seconds must be 1..120"));
    }
    FString Error;
    auto Data = Capture::Submit(RequestId, Name, Target, bShowUi, Timeout, bDryRun, Error);
    if (!Data.IsValid())
    {
        return MakeOperationError(Operation, RequestId, Error, TEXT("viewport capture was not admitted"));
    }
    auto Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
