#pragma once

#include "CoreMinimal.h"

class FRDGBuilder;
class FSceneView;
class FJsonObject;
struct FPostProcessingInputs;
struct FPostProcessMaterialInputs;
struct FScreenPassTexture;

namespace UeNodeNexusBridge::Diagnostics
{
bool QueueViewportPixelProbe(FIntRect Rect, uint32 ViewKey, int32 TemporalSample, int32 FrameMod8, double TimeoutSeconds);
TSharedPtr<FJsonObject> ReadViewportPixelProbe();
void ObserveViewportPixels_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
    const FPostProcessingInputs& Inputs);
void StopViewportPixelProbe_RenderThread();
bool HasViewportPixelProbe_RenderThread(const FSceneView& View);
FScreenPassTexture ObserveViewportPostProcess_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View,
    const FPostProcessMaterialInputs& Inputs, const TCHAR* Stage);
}
