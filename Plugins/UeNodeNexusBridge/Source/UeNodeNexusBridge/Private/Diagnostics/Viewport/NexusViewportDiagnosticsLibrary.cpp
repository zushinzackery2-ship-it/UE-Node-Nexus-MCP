#include "Diagnostics/NexusViewportDiagnosticsLibrary.h"

#include "Editor.h"
#include "CameraController.h"
#include "Engine/Engine.h"
#include "LevelEditorViewport.h"
#include "Misc/Guid.h"
#include "NexusViewportState.h"
#include "NexusViewportShaderPhase.h"
#include "NexusViewportPixelProbe.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
#include "SceneRendering.h"
#include "ScenePrivate.h"
#include "UeNodeNexusBridgeJson.h"
#include "Viewport/Capture/NexusCapture.h"

FString UNexusViewportDiagnosticsLibrary::PrimaryViewportStateJson()
{
    return UeNodeNexusBridge::SerializeJsonObjectToString(UeNodeNexusBridge::Diagnostics::ReadPrimaryViewport());
}

FString UNexusViewportDiagnosticsLibrary::SetPrimaryViewportInputEnabledJson(bool Enabled, int32 ViewKey)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("applied"), false);
    FLevelEditorViewportClient* Client = nullptr;
    if (IsInGameThread() && GEditor)
    {
        for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
        {
            FSceneViewStateInterface* State = Candidate->ViewState.GetReference();
            if (State && ((ViewKey == 0 && Candidate == GCurrentLevelEditingViewportClient)
                || (ViewKey > 0 && State->GetViewKey() == static_cast<uint32>(ViewKey))))
            {
                Client = Candidate;
                break;
            }
        }
    }
    if (Client)
    {
        Result->SetBoolField(TEXT("enabled_before"), !Client->bDisableInput);
        Result->SetBoolField(TEXT("transition_before"), Client->GetViewTransform().IsPlaying());
        if (!Enabled)
        {
            if (!Client->bDisableInput)
            {
                Client->LostFocus(Client->Viewport);
            }
            Client->GetCameraController()->ResetVelocity();
            Client->GetViewTransform().TransitionToLocation(Client->GetViewLocation(), TWeakPtr<SWidget>(), true);
        }
        else if (Client->bDisableInput)
        {
            Client->ReceivedFocus(Client->Viewport);
        }
        Client->bDisableInput = !Enabled;
        Result->SetBoolField(TEXT("enabled_after"), !Client->bDisableInput);
        Result->SetBoolField(TEXT("transition_after"), Client->GetViewTransform().IsPlaying());
        Result->SetBoolField(TEXT("motion_cleared"), !Enabled);
        Result->SetNumberField(TEXT("view_key"), Client->ViewState.GetReference()->GetViewKey());
        Result->SetBoolField(TEXT("applied"), true);
    }
    return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
}

FString UNexusViewportDiagnosticsLibrary::ResetPrimaryViewportHistoryJson()
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("reset"), false);
    FLevelEditorViewportClient* Client = IsInGameThread() ? GCurrentLevelEditingViewportClient : nullptr;
    if (!Client || !Client->ViewState.GetReference())
    {
        return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
    }
    FSceneViewStateInterface* State = Client->ViewState.GetReference();
    ENQUEUE_RENDER_COMMAND(NexusResetPrimaryHistory)
    ([State, Result](FRHICommandListImmediate&)
    {
        FSceneViewState* Concrete = State->GetConcreteViewState();
        Result->SetBoolField(TEXT("tsr_history_before"), Concrete->PrevFrameViewInfo.TSRHistory.IsValid());
        Result->SetNumberField(TEXT("tsr_frames_before"), Concrete->PrevFrameViewInfo.TSRHistory.AccumulatedFrameCount);
        State->ResetViewState();
        // UE5.5 ResetViewState releases eye adaptation and occlusion resources only.
        Result->SetBoolField(TEXT("tsr_history_after_seed_reset"), Concrete->PrevFrameViewInfo.TSRHistory.IsValid());
        Concrete->PrevFrameViewInfo = FPreviousViewInfo();
        Result->SetBoolField(TEXT("temporal_histories_released"),
            !Concrete->PrevFrameViewInfo.TSRHistory.IsValid() && !Concrete->PrevFrameViewInfo.TemporalAAHistory.IsValid()
            && !Concrete->PrevFrameViewInfo.CustomSSRInput.IsValid());
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    Result->SetBoolField(TEXT("reset"), true);
    Result->SetNumberField(TEXT("view_key"), State->GetViewKey());
    Result->SetNumberField(TEXT("engine_frame"), static_cast<double>(GFrameCounter));
    return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
}

static FString CaptureAtPhase(const FString& Filename,
    int32 TemporalSample, int32 FrameMod8, float TimeoutSeconds, bool bShaderPhase)
{
    auto Rejected = MakeShared<FJsonObject>();
    Rejected->SetBoolField(TEXT("requested"), false);
    Rejected->SetStringField(TEXT("state"), TEXT("rejected"));
    if (!UeNodeNexusBridge::Capture::IsSafeScreenshotName(Filename)
        || TemporalSample < 0 || FrameMod8 < 0 || FrameMod8 > 7
        || !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds < 1 || TimeoutSeconds > 120)
    {
        Rejected->SetStringField(TEXT("error_code"), TEXT("invalid_request"));
        return UeNodeNexusBridge::SerializeJsonObjectToString(Rejected);
    }
    auto Phase = MakeShared<FJsonObject>();
    if (bShaderPhase)
    {
        Phase->SetNumberField(TEXT("shader_temporal_sample"), TemporalSample);
        Phase->SetNumberField(TEXT("shader_frame_mod8"), FrameMod8);
        Phase->SetNumberField(TEXT("output_frame_mod8"), FrameMod8);
    }
    else
    {
        Phase->SetNumberField(TEXT("temporal_sample"), TemporalSample);
        Phase->SetNumberField(TEXT("render_frame_mod8"), FrameMod8);
    }
    FString Error;
    auto Receipt = UeNodeNexusBridge::Capture::Submit(FGuid::NewGuid().ToString(EGuidFormats::Digits),
        Filename, TEXT("level"), false, TimeoutSeconds, false, Error, false, Phase);
    if (!Receipt.IsValid())
    {
        Rejected->SetStringField(TEXT("error_code"), Error);
        return UeNodeNexusBridge::SerializeJsonObjectToString(Rejected);
    }
    return UeNodeNexusBridge::SerializeJsonObjectToString(Receipt);
}

FString UNexusViewportDiagnosticsLibrary::CapturePrimaryAtPhaseJson(const FString& Filename,
    int32 TemporalSample, int32 RenderFrameMod8, float TimeoutSeconds)
{
    return CaptureAtPhase(Filename, TemporalSample, RenderFrameMod8, TimeoutSeconds, false);
}

FString UNexusViewportDiagnosticsLibrary::CapturePrimaryAtShaderPhaseJson(const FString& Filename,
    int32 TemporalSample, int32 ShaderFrameMod8, float TimeoutSeconds)
{
    return CaptureAtPhase(Filename, TemporalSample, ShaderFrameMod8, TimeoutSeconds, true);
}

FString UNexusViewportDiagnosticsLibrary::QueuePrimaryPixelProbeJson(int32 X, int32 Y,
    int32 Width, int32 Height, int32 TemporalSample, int32 ShaderFrameMod8, float TimeoutSeconds)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("requested"), false);
    FLevelEditorViewportClient* Client = IsInGameThread() ? GCurrentLevelEditingViewportClient : nullptr;
    if (!Client || !Client->Viewport || !Client->ViewState.GetReference() || X < 0 || Y < 0
        || Width < 1 || Height < 1 || Width > 32 || Height > 32
        || TemporalSample < 0 || ShaderFrameMod8 < 0 || ShaderFrameMod8 > 7
        || !FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds < 1 || TimeoutSeconds > 120
        || X > Client->Viewport->GetSizeXY().X - Width || Y > Client->Viewport->GetSizeXY().Y - Height)
    {
        Result->SetStringField(TEXT("error_code"), TEXT("invalid_request"));
        return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
    }
    UeNodeNexusBridge::Diagnostics::BindViewportShaderPhase(Client->Viewport);
    const uint32 ViewKey = Client->ViewState.GetReference()->GetViewKey();
    const bool bAccepted = UeNodeNexusBridge::Diagnostics::QueueViewportPixelProbe(
        FIntRect(X, Y, X + Width, Y + Height), ViewKey, TemporalSample, ShaderFrameMod8, TimeoutSeconds);
    Result->SetBoolField(TEXT("requested"), bAccepted);
    Result->SetStringField(TEXT("state"), bAccepted ? TEXT("queued") : TEXT("rejected"));
    Result->SetStringField(TEXT("error_code"), bAccepted ? TEXT("") : TEXT("pixel_probe_busy"));
    Result->SetNumberField(TEXT("view_key"), ViewKey);
    return UeNodeNexusBridge::SerializeJsonObjectToString(Result);
}

FString UNexusViewportDiagnosticsLibrary::ReadPrimaryPixelProbeJson()
{
    return UeNodeNexusBridge::SerializeJsonObjectToString(UeNodeNexusBridge::Diagnostics::ReadViewportPixelProbe());
}

bool UNexusViewportDiagnosticsLibrary::ProfilePrimaryGpu()
{
    FLevelEditorViewportClient* Client = IsInGameThread() ? GCurrentLevelEditingViewportClient : nullptr;
    return Client && GEngine && GEngine->Exec(Client->GetWorld(), TEXT("PROFILEGPU"));
}
