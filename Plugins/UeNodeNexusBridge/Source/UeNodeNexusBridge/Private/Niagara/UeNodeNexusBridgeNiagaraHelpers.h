#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterialInterface;
class UNiagaraRendererProperties;
class UNiagaraSystem;

namespace UeNodeNexusBridge
{
UNiagaraSystem* LoadNiagaraSystemFromPayload(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutResponse);

TSharedPtr<FJsonObject> MakeNiagaraAssetData(UNiagaraSystem* System);
void AddNiagaraToolBoundary(TSharedPtr<FJsonObject> Data);
bool NiagaraSystemHasEmitterStack(UNiagaraSystem* System);
int32 CountNiagaraRenderers(UNiagaraSystem* System);
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
