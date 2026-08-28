#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UMaterialInterface;
class UNiagaraRendererProperties;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;

namespace UeNodeNexusBridge
{
// Reads an optional integer payload field; leaves INDEX_NONE and returns
// false when missing. Shared by every emitter/renderer-indexed operation.
bool ReadNiagaraIndexField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue);
// Resolves payload.emitter_index to an emitter handle or fills OutError with
// the canonical invalid_emitter_index envelope.
FNiagaraEmitterHandle* ResolveNiagaraEmitterHandle(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId);
UNiagaraSystem* LoadNiagaraSystemFromPayload(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutResponse);

TSharedPtr<FJsonObject> MakeNiagaraAssetData(UNiagaraSystem* System);
TSharedPtr<FJsonObject> MakeNiagaraAssetSummaryData(UNiagaraSystem* System);
bool NiagaraSystemHasEmitterStack(UNiagaraSystem* System);
int32 CountNiagaraRenderers(UNiagaraSystem* System);
int32 CountEnabledNiagaraEmitters(UNiagaraSystem* System);
int32 CountEnabledNiagaraRenderers(UNiagaraSystem* System);
int32 CountNiagaraReadinessIssues(UNiagaraSystem* System);
TSharedPtr<FJsonObject> MakeNiagaraBoundaryWarning(UNiagaraSystem* System, const FString& Code, const FString& Message);
void AppendNiagaraEmptySystemWarning(UNiagaraSystem* System, TArray<TSharedPtr<FJsonValue>>& Warnings);
TSharedPtr<FJsonObject> MakeNiagaraEmitterJson(UNiagaraSystem* System, int32 EmitterIndex);
TSharedPtr<FJsonValue> MakeNiagaraEmitterRow(UNiagaraSystem* System, int32 EmitterIndex);
FString GetRendererMaterialPath(UNiagaraRendererProperties* Renderer, int32 MaterialIndex);
bool SetRendererMaterial(UNiagaraRendererProperties* Renderer, UMaterialInterface* Material, int32 MaterialIndex);
TSharedPtr<FJsonObject> MakeNiagaraMaterialJson(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer, int32 MaterialIndex);
TSharedPtr<FJsonValue> MakeNiagaraMaterialRow(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer, int32 MaterialIndex);
bool SaveAssetPackage(UObject* Asset);
}
