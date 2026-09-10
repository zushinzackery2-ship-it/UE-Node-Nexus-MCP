#pragma once

#include "UeNodeNexusBridgeDiagnostics.h"

namespace UeNodeNexusBridge
{
FBridgeAssetCompileDiagnostics MaterialResourceStatus(UMaterialInterface* Material, bool bWait, bool bRenderFence = true);
void FinishRenderingUpdates();
bool EnsureMaterialParentReady(UObject* Asset, const TArray<TSharedPtr<FJsonValue>>& Plan, bool bWait, FString& Error);
TSharedPtr<FJsonObject> CompileAssetWrite(UObject* Asset, bool bRequested, bool bRun, TArray<TSharedPtr<FJsonValue>>& Diagnostics);
}
