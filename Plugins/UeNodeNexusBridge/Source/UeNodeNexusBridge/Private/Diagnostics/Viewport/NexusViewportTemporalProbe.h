#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

class FJsonObject;
class FSceneView;

namespace UeNodeNexusBridge::Diagnostics
{
using FTemporalProbeCopy = TFunctionRef<void(const TCHAR*, FRDGTextureRef, FIntRect, uint32)>;

void ObserveViewportTemporalInputs(FRDGBuilder& GraphBuilder, const FSceneView& View,
    FJsonObject& Result, FIntRect Rect, FTemporalProbeCopy Copy);
}
