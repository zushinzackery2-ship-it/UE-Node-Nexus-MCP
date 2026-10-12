#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "RHIGPUReadback.h"

namespace UeNodeNexusBridge::Diagnostics
{
struct FProbeTexture
{
    FString Name;
    EPixelFormat Format = PF_Unknown;
    FIntRect Rect;
    TUniquePtr<FRHIGPUTextureReadback> Readback;
};

struct FProbeRequest
{
    FIntRect Rect;
    uint32 ViewKey = 0;
    int32 TemporalSample = 0;
    int32 FrameMod8 = 0;
    double Deadline = 0;
    bool bQueued = false;
    bool bReading = false;
    TArray<FProbeTexture> Textures;
    TSharedPtr<FJsonObject> Result;
};

FProbeRequest& PixelProbeState_RenderThread();
void ExpirePixelProbe_RenderThread(FProbeRequest& Probe);
void CompletePixelProbeReadback_RenderThread(FProbeRequest& Probe);
}
