#pragma once

#include "CoreMinimal.h"

class FViewport;
class FJsonObject;

namespace UeNodeNexusBridge::Diagnostics
{
struct FViewportShaderPhase
{
    bool bAvailable = false;
    uint32 ViewKey = 0;
    uint32 EngineFrame = 0;
    uint32 ShaderFrame = 0;
    uint32 OutputFrame = 0;
    FVector4f TemporalAA = FVector4f(0, 0, 0, 0);
    FVector2f ViewSize = FVector2f(0, 0);
    FVector2f BufferSize = FVector2f(0, 0);
    FString World;
    int32 WorldType = INDEX_NONE;
    FString ViewportType;
    TArray<FString> PostProcessMaterials;
    bool bPostProcessing = false;
    bool bPostProcessMaterial = false;
    bool bTonemapper = false;
    float PreExposure = 0;
    float ExposureBias = 0;
    int32 ExposureMethod = 0;
};

void BindViewportShaderPhase(FViewport* Viewport);
FViewportShaderPhase ReadViewportShaderPhase_RenderThread();
bool AppendViewportRenderReceipt(const TSharedPtr<FJsonObject>& Receipt);
void StopViewportShaderPhase();
}
