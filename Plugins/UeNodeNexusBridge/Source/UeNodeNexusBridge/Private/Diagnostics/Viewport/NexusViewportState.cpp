#include "NexusViewportState.h"
#include "NexusViewportShaderPhase.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"
#include "SceneManagement.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "UnrealClient.h"

namespace UeNodeNexusBridge::Diagnostics
{
TSharedPtr<FJsonObject> ReadPrimaryViewport()
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("available"), false);
    FLevelEditorViewportClient* Client = IsInGameThread() ? GCurrentLevelEditingViewportClient : nullptr;
    if (!Client || !Client->Viewport || !Client->ViewState.GetReference())
    {
        return Result;
    }
    FSceneViewStateInterface* State = Client->ViewState.GetReference();
    BindViewportShaderPhase(Client->Viewport);
    const FIntPoint Size = Client->Viewport->GetSizeXY();
    if (Size.X <= 0 || Size.Y <= 0)
    {
        return Result;
    }
    uint32 Frame = 0;
    uint32 TemporalSample = 0;
    float PreExposure = 0;
    float Exposure = 0;
    FViewportShaderPhase Shader;
    ENQUEUE_RENDER_COMMAND(NexusReadCompletedViewportState)
    ([State, &Frame, &TemporalSample, &PreExposure, &Exposure, &Shader](FRHICommandListImmediate&)
    {
        Frame = State->GetOcclusionFrameCounter();
        TemporalSample = State->GetCurrentTemporalAASampleIndex();
        PreExposure = State->GetPreExposure();
        Exposure = State->GetLastEyeAdaptationExposure();
        Shader = ReadViewportShaderPhase_RenderThread();
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    Result->SetBoolField(TEXT("available"), true);
    Result->SetNumberField(TEXT("engine_frame"), static_cast<double>(GFrameCounter));
    Result->SetBoolField(TEXT("realtime"), Client->IsRealtime());
    Result->SetStringField(TEXT("realtime_override"), Client->GetRealtimeOverrideMessage().ToString());
    Result->SetBoolField(TEXT("game_view"), Client->IsInGameView());
    Result->SetBoolField(TEXT("temporal_aa"), Client->EngineShowFlags.TemporalAA);
    Result->SetBoolField(TEXT("anti_aliasing"), Client->EngineShowFlags.AntiAliasing);
    Result->SetBoolField(TEXT("post_processing"), Client->EngineShowFlags.PostProcessing);
    Result->SetNumberField(TEXT("width"), Size.X);
    Result->SetNumberField(TEXT("height"), Size.Y);
    Result->SetNumberField(TEXT("view_key"), State->GetViewKey());
    Result->SetBoolField(TEXT("camera_input_disabled"), Client->bDisableInput);
    Result->SetBoolField(TEXT("camera_transition_active"), Client->GetViewTransform().IsPlaying());
    Result->SetBoolField(TEXT("camera_right_mouse_down"), Client->Viewport->KeyState(EKeys::RightMouseButton));
    Result->SetBoolField(TEXT("joystick_controls"), GetDefault<ULevelEditorViewportSettings>()->bLevelEditorJoystickControls);
    Result->SetNumberField(TEXT("render_frame"), Frame);
    Result->SetNumberField(TEXT("render_frame_mod8"), Frame % 8);
    Result->SetNumberField(TEXT("temporal_sample"), TemporalSample);
    Result->SetNumberField(TEXT("pre_exposure"), PreExposure);
    Result->SetNumberField(TEXT("exposure"), Exposure);
    const bool bShaderAvailable = Shader.bAvailable && Shader.ViewKey == State->GetViewKey();
    Result->SetBoolField(TEXT("shader_available"), bShaderAvailable);
    if (bShaderAvailable)
    {
        Result->SetNumberField(TEXT("shader_engine_frame"), Shader.EngineFrame);
        Result->SetNumberField(TEXT("shader_frame_index"), Shader.ShaderFrame);
        Result->SetNumberField(TEXT("shader_frame_mod8"), Shader.ShaderFrame % 8);
        Result->SetNumberField(TEXT("output_frame_index"), Shader.OutputFrame);
        Result->SetNumberField(TEXT("output_frame_mod8"), Shader.OutputFrame % 8);
        Result->SetNumberField(TEXT("shader_temporal_sample"), Shader.TemporalAA.X);
        Result->SetNumberField(TEXT("shader_temporal_count"), Shader.TemporalAA.Y);
        Result->SetNumberField(TEXT("shader_jitter_x"), Shader.TemporalAA.Z);
        Result->SetNumberField(TEXT("shader_jitter_y"), Shader.TemporalAA.W);
        Result->SetNumberField(TEXT("shader_width"), Shader.ViewSize.X);
        Result->SetNumberField(TEXT("shader_height"), Shader.ViewSize.Y);
        Result->SetNumberField(TEXT("buffer_width"), Shader.BufferSize.X);
        Result->SetNumberField(TEXT("buffer_height"), Shader.BufferSize.Y);
    }
    return Result;
}
}
