#include "NexusCapture.h"

#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "NexusLifecycle.h"
#include "Diagnostics/Viewport/NexusViewportState.h"
#include "Diagnostics/Viewport/NexusViewportShaderPhase.h"
#include "Diagnostics/Viewport/NexusViewportDiagnosticsLog.h"
#include "Engine/GameViewportClient.h"
#include "Transport/Safety/NexusAsyncWork.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "UnrealClient.h"

namespace UeNodeNexusBridge::Capture
{
struct FJob
{
    TSharedPtr<FJsonObject> Receipt;
    FString Target;
    FString RequestId;
    double Deadline = 0;
    bool bShowUi = false;
    bool bSubmitted = false;
    bool bProcessed = false;
    bool bRetainScope = false;
    TSharedPtr<FJsonObject> Phase;
    FViewport* Viewport = nullptr;
};

static TUniquePtr<FJob> Active;
static FTSTicker::FDelegateHandle Ticker;
static FDelegateHandle Processed;
static FDelegateHandle Rendered;

static void Finish(const FString& Code)
{
    if (!Active)
    {
        return;
    }
    const FString RequestId = Active->RequestId;
    const FString Path = Active->Receipt->GetStringField(TEXT("file_path"));
    if (FScreenshotRequest::GetFilename() == Path)
    {
        FScreenshotRequest::Reset();
    }
    FString Failure = Code;
    FString Digest;
    int32 Width = 0, Height = 0;
    if (Failure.IsEmpty() && !VerifyImage(Path, Width, Height, &Digest))
    {
        Failure = TEXT("capture_invalid_image");
    }
    Active->Receipt->SetStringField(TEXT("state"), Failure.IsEmpty() ? TEXT("completed") : TEXT("failed"));
    Active->Receipt->SetStringField(TEXT("error_code"), Failure);
    Active->Receipt->SetStringField(TEXT("finished_at"), FDateTime::UtcNow().ToIso8601());
    Active->Receipt->SetBoolField(TEXT("exists"), Failure.IsEmpty());
    Active->Receipt->SetNumberField(TEXT("width"), Width);
    Active->Receipt->SetNumberField(TEXT("height"), Height);
    Active->Receipt->SetStringField(TEXT("sha1"), Digest);
    Active->Receipt->SetNumberField(TEXT("size_bytes"), IFileManager::Get().FileSize(*Path));
    if (!WriteReceipt(Active->Receipt))
    {
        Failure = TEXT("capture_journal_failed");
        UE_LOG(LogTemp, Error, TEXT("Nexus request=%s capture receipt could not be persisted"), *RequestId);
    }
    UE_LOG(LogNexusViewportDiagnostics, Display, TEXT("capture request=%s state=%s reason=%s file=%s"),
        *RequestId, Failure.IsEmpty() ? TEXT("completed") : TEXT("failed"), *Failure, *Path);
    FScreenshotRequest::OnScreenshotRequestProcessed().Remove(Processed);
    UGameViewportClient::OnViewportRendered().Remove(Rendered);
    Processed.Reset();
    Rendered.Reset();
    const bool bRetainScope = Active->bRetainScope;
    Active.Reset();
    if (!bRetainScope)
    {
        Safety::FinishAsync(RequestId, Failure.IsEmpty(), Failure);
    }
}

static void OnRendered(FViewport* Viewport)
{
    if (!Active || Active->bSubmitted || IsBridgeDispatchActive() || !Safety::EngineBusyReason().IsEmpty())
    {
        return;
    }
    if (Viewport != Active->Viewport)
    {
        return;
    }
    TSharedPtr<FJsonObject> State;
    if (Active->Phase.IsValid())
    {
        State = Diagnostics::ReadPrimaryViewport();
        if (!State->GetBoolField(TEXT("available")))
        {
            return;
        }
        for (const auto& Requested : Active->Phase->Values)
        {
            double Observed = 0;
            if (!State->TryGetNumberField(Requested.Key, Observed) || Observed != Requested.Value->AsNumber())
            {
                return;
            }
        }
    }
    if (FScreenshotRequest::IsScreenshotRequested() || GIsHighResScreenshot || GIsDumpingMovie)
    {
        Finish(TEXT("capture_external_busy"));
        return;
    }
    const FString Path = Active->Receipt->GetStringField(TEXT("file_path"));
    Active->Receipt->SetStringField(TEXT("state"), TEXT("rendering"));
    Active->Receipt->SetNumberField(TEXT("engine_frame"), GFrameCounter);
    // Freeze the actual producing view at submission. Reading it on a later
    // completion tick can accidentally bind the image to a newer rendered frame.
    if (!Diagnostics::AppendViewportRenderReceipt(Active->Receipt))
    {
        Finish(TEXT("capture_producer_mismatch"));
        return;
    }
    if (State.IsValid())
    {
        Active->Receipt->SetObjectField(TEXT("render_state"), State);
    }
    if (!WriteReceipt(Active->Receipt))
    {
        Finish(TEXT("capture_journal_failed"));
        return;
    }
    Active->bSubmitted = true;
    Processed = FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([]()
    {
        if (Active)
        {
            Active->bProcessed = true;
        }
    });
    FScreenshotRequest::RequestScreenshot(Path, Active->bShowUi, false);
}

static bool Tick(float Delta)
{
    if (!Active)
    {
        return false;
    }
    FString Resolved;
    if (ResolveViewport(Active->Target, Resolved) != Active->Viewport)
    {
        Finish(TEXT("capture_viewport_changed"));
        return false;
    }
    const FString Path = Active->Receipt->GetStringField(TEXT("file_path"));
    if (FPlatformTime::Seconds() >= Active->Deadline)
    {
        Finish(TEXT("capture_timeout"));
        return false;
    }
    if (Active->bProcessed)
    {
        Finish(FString());
        return false;
    }
    if (Active->bSubmitted && FScreenshotRequest::GetFilename() != Path)
    {
        Finish(TEXT("capture_request_replaced"));
    }
    return Active.IsValid();
}

TSharedPtr<FJsonObject> Submit(const FString& RequestId, const FString& BaseName,
    const FString& Target, bool bShowUi, double Timeout, bool bDryRun, FString& Error, bool bRetainScope,
    const TSharedPtr<FJsonObject>& Phase)
{
    const bool bAdmissionValid = bRetainScope ? Safety::Holds(RequestId) : Safety::AsyncBusyReason().IsEmpty();
    if (!IsInGameThread() || Active || !bAdmissionValid
        || FScreenshotRequest::IsScreenshotRequested() || GIsHighResScreenshot || GIsDumpingMovie)
    {
        Error = TEXT("capture_busy");
        return nullptr;
    }
    FString Resolved;
    FViewport* Viewport = ResolveViewport(Target, Resolved);
    if (!Viewport)
    {
        Error = TEXT("viewport_unavailable");
        return nullptr;
    }
    const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / (BaseName + TEXT("_") + Id + TEXT(".png")));
    auto Receipt = MakeShared<FJsonObject>();
    Receipt->SetNumberField(TEXT("capture_protocol"), 1);
    Receipt->SetStringField(TEXT("capture_id"), Id);
    Receipt->SetStringField(TEXT("request_id"), RequestId);
    Receipt->SetStringField(TEXT("file_path"), Path);
    Receipt->SetStringField(TEXT("receipt_path"), Path + TEXT(".capture.json"));
    Receipt->SetStringField(TEXT("target"), Resolved);
    Receipt->SetStringField(TEXT("state"), bDryRun ? TEXT("preview") : TEXT("queued"));
    Receipt->SetStringField(TEXT("started_at"), FDateTime::UtcNow().ToIso8601());
    Receipt->SetNumberField(TEXT("producer_pid"), FPlatformProcess::GetCurrentProcessId());
    auto Producer = MakeShared<FJsonObject>();
    const auto Identity = NexusLifecycle::Snapshot();
    const TCHAR* ProducerFields[] =
    {
        TEXT("pid"), TEXT("process_created"), TEXT("executable")
    };
    for (const TCHAR* Key : ProducerFields)
    {
        if (const auto* Value = Identity->Values.Find(Key))
        {
            Producer->SetField(Key, *Value);
        }
    }
    Receipt->SetObjectField(TEXT("producer"), Producer);
    Receipt->SetBoolField(TEXT("dry_run"), bDryRun);
    Receipt->SetBoolField(TEXT("requested"), !bDryRun);
    Receipt->SetBoolField(TEXT("exists"), false);
    Receipt->SetStringField(TEXT("scheduling"), TEXT("target_normal_draw"));
    if (Phase.IsValid())
    {
        Receipt->SetObjectField(TEXT("requested_phase"), Phase);
    }
    if (bDryRun)
    {
        return Receipt;
    }
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true) || !WriteReceipt(Receipt))
    {
        Error = TEXT("capture_journal_failed");
        return nullptr;
    }
    if (!bRetainScope && !Safety::BeginAsync(RequestId, TEXT("viewport_capture")))
    {
        Error = TEXT("capture_busy");
        Receipt->SetStringField(TEXT("state"), TEXT("failed"));
        Receipt->SetStringField(TEXT("error_code"), Error);
        WriteReceipt(Receipt);
        return nullptr;
    }
    Active = MakeUnique<FJob>();
    Active->Receipt = Receipt;
    Active->RequestId = RequestId;
    Active->Target = Target;
    Active->bShowUi = bShowUi;
    Active->bRetainScope = bRetainScope;
    Active->Phase = Phase;
    Active->Viewport = Viewport;
    // Identity data for the completion receipt must be current even for a plain
    // capture, so the extension binds on every admitted job.
    Diagnostics::BindViewportShaderPhase(Viewport);
    Active->Deadline = FPlatformTime::Seconds() + Timeout;
    Rendered = UGameViewportClient::OnViewportRendered().AddStatic(&OnRendered);
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
    UE_LOG(LogNexusViewportDiagnostics, Display, TEXT("capture request=%s state=queued target=%s timeout=%.3f"),
        *RequestId, *Resolved, Timeout);
    return MakeShared<FJsonObject>(*Receipt);
}

void Stop()
{
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    Finish(TEXT("capture_interrupted"));
}
}
