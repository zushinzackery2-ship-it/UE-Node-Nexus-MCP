#include "NexusPixelProbeState.h"
#include "../NexusViewportDiagnosticsLog.h"
#include "../NexusViewportPixelProbe.h"

#include "HAL/PlatformTime.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"

namespace UeNodeNexusBridge::Diagnostics
{
FProbeRequest& PixelProbeState_RenderThread()
{
    check(IsInRenderingThread());
    static FProbeRequest Probe;
    return Probe;
}

void ExpirePixelProbe_RenderThread(FProbeRequest& Probe)
{
    check(IsInRenderingThread());
    if ((Probe.bQueued || Probe.bReading) && FPlatformTime::Seconds() >= Probe.Deadline
        && Probe.Result->GetStringField(TEXT("state")) != TEXT("failed"))
    {
        Probe.bQueued = false;
        Probe.Result->SetStringField(TEXT("state"), TEXT("failed"));
        Probe.Result->SetStringField(TEXT("error_code"), TEXT("pixel_probe_timeout"));
        UE_LOG(LogNexusViewportDiagnostics, Display, TEXT("pixel_probe view=%u state=failed reason=pixel_probe_timeout"), Probe.ViewKey);
    }
}

bool QueueViewportPixelProbe(FIntRect Rect, uint32 ViewKey, int32 TemporalSample, int32 FrameMod8, double TimeoutSeconds)
{
    check(IsInGameThread());
    bool bAccepted = false;
    ENQUEUE_RENDER_COMMAND(NexusQueuePixelProbe)
    ([Rect, ViewKey, TemporalSample, FrameMod8, TimeoutSeconds, &bAccepted](FRHICommandListImmediate&)
    {
        FProbeRequest& Probe = PixelProbeState_RenderThread();
        CompletePixelProbeReadback_RenderThread(Probe);
        if (Probe.bQueued || Probe.bReading)
        {
            return;
        }
        Probe = FProbeRequest();
        Probe.Rect = Rect;
        Probe.ViewKey = ViewKey;
        Probe.TemporalSample = TemporalSample;
        Probe.FrameMod8 = FrameMod8;
        Probe.Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        Probe.bQueued = true;
        Probe.Result = MakeShared<FJsonObject>();
        Probe.Result->SetStringField(TEXT("state"), TEXT("queued"));
        Probe.Result->SetNumberField(TEXT("view_key"), ViewKey);
        Probe.Result->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
        bAccepted = true;
        UE_LOG(LogNexusViewportDiagnostics, Display,
            TEXT("pixel_probe view=%u state=queued rect=%d,%d,%d,%d sample=%d mod8=%d timeout=%.3f"),
            ViewKey, Rect.Min.X, Rect.Min.Y, Rect.Width(), Rect.Height(), TemporalSample, FrameMod8, TimeoutSeconds);
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    return bAccepted;
}

TSharedPtr<FJsonObject> ReadViewportPixelProbe()
{
    auto Result = MakeShared<FJsonObject>();
    if (!IsInGameThread())
    {
        Result->SetStringField(TEXT("state"), TEXT("rejected"));
        Result->SetStringField(TEXT("error_code"), TEXT("game_thread_required"));
        return Result;
    }
    ENQUEUE_RENDER_COMMAND(NexusReadPixelProbe)
    ([&Result](FRHICommandListImmediate&)
    {
        FProbeRequest& Probe = PixelProbeState_RenderThread();
        CompletePixelProbeReadback_RenderThread(Probe);
        if (Probe.Result.IsValid())
        {
            Result = MakeShared<FJsonObject>(*Probe.Result);
        }
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    return Result;
}

void StopViewportPixelProbe_RenderThread()
{
    check(IsInRenderingThread());
    PixelProbeState_RenderThread() = FProbeRequest();
}
}
