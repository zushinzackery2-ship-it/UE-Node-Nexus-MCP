#include "UeNodeNexusBridgeOperations.h"

#include "Editor.h"
#include "HAL/FileManager.h"
#include "LevelEditorViewport.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "UeNodeNexusBridgeJson.h"
#include "UnrealClient.h"

namespace UeNodeNexusBridge
{
namespace
{
// "level" (default) is the level editor viewport the user is looking through;
// "active" is whatever Slate last focused. After an editor restart the active
// viewport is routinely a reopened Material Editor preview, which is how a
// screenshot of "the scene" once came back as a rain-glass swatch.
FViewport* ResolveCaptureViewport(const FString& Target, FString& OutResolved)
{
    if (!Target.Equals(TEXT("active"), ESearchCase::IgnoreCase))
    {
        if (GCurrentLevelEditingViewportClient != nullptr && GCurrentLevelEditingViewportClient->Viewport != nullptr)
        {
            OutResolved = TEXT("level_current");
            return GCurrentLevelEditingViewportClient->Viewport;
        }
        FViewport* AnyLevelViewport = nullptr;
        for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
        {
            if (Client == nullptr || Client->Viewport == nullptr)
            {
                continue;
            }
            if (Client->IsPerspective())
            {
                OutResolved = TEXT("level_perspective");
                return Client->Viewport;
            }
            AnyLevelViewport = AnyLevelViewport ? AnyLevelViewport : Client->Viewport;
        }
        if (AnyLevelViewport != nullptr)
        {
            OutResolved = TEXT("level_any");
            return AnyLevelViewport;
        }
    }
    OutResolved = TEXT("active");
    return GEditor->GetActiveViewport();
}

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

    FString Target = TEXT("level");
    Payload->TryGetStringField(TEXT("target"), Target);
    FString ResolvedTarget;
    FViewport* Viewport = GEditor != nullptr ? ResolveCaptureViewport(Target, ResolvedTarget) : nullptr;
    if (Viewport == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("viewport_unavailable"), TEXT("No editor viewport is available for the requested target"));
    }

    bool bDryRun = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bShowUi = false;
    Payload->TryGetBoolField(TEXT("show_ui"), bShowUi);

    const FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ScreenShotDir(), BaseName + TEXT(".png")));

    bool bExists = false;
    if (!bDryRun)
    {
        // Draw the frame here instead of waiting for Slate: a background editor
        // throttles its ticks, so "after the next redraw" was anywhere from 2 s to
        // never. FViewport::Draw services the pending screenshot request itself,
        // so by the time it returns the PNG is on disk (or provably is not).
        FScreenshotRequest::RequestScreenshot(FilePath, bShowUi, false);
        Viewport->Draw(true);
        bExists = IFileManager::Get().FileExists(*FilePath);
        if (!bExists)
        {
            // Some viewport clients defer the request to their own tick; keep the
            // old behaviour as a fallback so the file still lands on the next frame.
            GEditor->RedrawAllViewports(true);
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("file_path"), FilePath);
    Data->SetStringField(TEXT("target"), ResolvedTarget);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("requested"), !bDryRun);
    Data->SetBoolField(TEXT("exists"), bExists);
    Data->SetStringField(TEXT("note"), bExists
        ? TEXT("PNG written synchronously; file_path is absolute.")
        : TEXT("PNG not written yet; it lands after the next viewport redraw. Poll the absolute file_path on disk."));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
