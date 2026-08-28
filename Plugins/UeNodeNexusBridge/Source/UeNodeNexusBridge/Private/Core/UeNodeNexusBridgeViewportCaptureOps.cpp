#include "UeNodeNexusBridgeOperations.h"

#include "Editor.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "UeNodeNexusBridgeJson.h"
#include "UnrealClient.h"

namespace UeNodeNexusBridge
{
namespace
{
// Screenshot names are plain basenames inside the project ScreenShotDir; path
// separators and dots are rejected so the payload cannot escape that folder.
bool IsSafeScreenshotName(const FString& Name)
{
    if (Name.IsEmpty())
    {
        return false;
    }
    for (const TCHAR Char : Name)
    {
        const bool bAllowed =
            (Char >= TEXT('a') && Char <= TEXT('z')) ||
            (Char >= TEXT('A') && Char <= TEXT('Z')) ||
            (Char >= TEXT('0') && Char <= TEXT('9')) ||
            Char == TEXT('_') || Char == TEXT('-');
        if (!bAllowed)
        {
            return false;
        }
    }
    return true;
}
}

TSharedPtr<FJsonObject> HandleViewportCapture(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString BaseName;
    Payload->TryGetStringField(TEXT("filename"), BaseName);
    if (!BaseName.IsEmpty() && !IsSafeScreenshotName(BaseName))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("filename may only contain letters, digits, underscore, and dash"));
    }
    if (BaseName.IsEmpty())
    {
        BaseName = FString::Printf(TEXT("UeNodeNexus_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
    }

    if (GEditor == nullptr || GEditor->GetActiveViewport() == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("viewport_unavailable"), TEXT("No active editor viewport is available"));
    }

    bool bDryRun = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bShowUi = false;
    Payload->TryGetBoolField(TEXT("show_ui"), bShowUi);

    const FString FilePath = FPaths::Combine(FPaths::ScreenShotDir(), BaseName + TEXT(".png"));

    if (!bDryRun)
    {
        // The screenshot is taken when the viewport next redraws, after this
        // handler has returned; the redraw request below schedules that frame.
        FScreenshotRequest::RequestScreenshot(FilePath, bShowUi, false);
        GEditor->RedrawAllViewports(true);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("requested"), !bDryRun);
    Data->SetStringField(TEXT("note"), TEXT("PNG is written asynchronously after the next viewport redraw; poll viewport_capture_status with this file_path."));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
