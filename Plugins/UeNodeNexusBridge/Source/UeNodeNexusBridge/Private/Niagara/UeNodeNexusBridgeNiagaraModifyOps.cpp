#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static bool ReadIndexField(const TSharedPtr<FJsonObject>& Payload, const FString& Field, int32& OutValue)
{
    double Number = -1.0;
    if (!Payload->TryGetNumberField(Field, Number))
    {
        OutValue = INDEX_NONE;
        return false;
    }
    OutValue = static_cast<int32>(Number);
    return true;
}

TSharedPtr<FJsonObject> HandleNiagaraMaterialsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        const FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[EmitterIndex].GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
        for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
        {
            Items.Add(bCompact ? MakeNiagaraMaterialRow(EmitterIndex, RendererIndex, Renderers[RendererIndex], 0) : MakeShared<FJsonValueObject>(MakeNiagaraMaterialJson(EmitterIndex, RendererIndex, Renderers[RendererIndex], 0)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_materials_compact") : TEXT("full"));
    Data->SetArrayField(TEXT("columns"), {
        MakeShared<FJsonValueString>(TEXT("emitter_index")),
        MakeShared<FJsonValueString>(TEXT("renderer_index")),
        MakeShared<FJsonValueString>(TEXT("material_index")),
        MakeShared<FJsonValueString>(TEXT("renderer_class")),
        MakeShared<FJsonValueString>(TEXT("material_path"))
    });
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraMaterialsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString MaterialPath;
    if (!Payload->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("material_path is required")));
        return Response;
    }
    UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (Material == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("material_not_found"), TEXT("Material interface could not be loaded")));
        return Response;
    }

    int32 WantedEmitter = INDEX_NONE;
    int32 WantedRenderer = INDEX_NONE;
    ReadIndexField(Payload, TEXT("emitter_index"), WantedEmitter);
    ReadIndexField(Payload, TEXT("renderer_index"), WantedRenderer);
    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    int32 Planned = 0;
    int32 Changed = 0;
    for (int32 EmitterIndex = 0; EmitterIndex < System->GetEmitterHandles().Num(); ++EmitterIndex)
    {
        if (WantedEmitter != INDEX_NONE && WantedEmitter != EmitterIndex)
        {
            continue;
        }
        const FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[EmitterIndex].GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
        for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
        {
            if (WantedRenderer != INDEX_NONE && WantedRenderer != RendererIndex)
            {
                continue;
            }
            ++Planned;
            if (!bDryRun && SetRendererMaterial(Renderers[RendererIndex], Material, 0))
            {
                ++Changed;
            }
        }
    }

    if (!bDryRun && Changed > 0)
    {
        System->Modify();
        System->MarkPackageDirty();
        System->PostEditChange();
    }
    const bool bSaved = bSave && Changed > 0 && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);
    Data->SetBoolField(TEXT("saved"), bSaved);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraCompile(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    bool bWait = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("wait"), bWait);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    const bool bRequested = System->RequestCompile(true);
    if (bWait)
    {
        System->WaitForCompilationComplete(false, false);
    }
    const bool bSaved = bSave && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetBoolField(TEXT("requested"), bRequested);
    Data->SetBoolField(TEXT("waited"), bWait);
    Data->SetBoolField(TEXT("ready_to_run"), System->IsReadyToRun());
    Data->SetBoolField(TEXT("needs_compile"), System->NeedsRequestCompile());
    Data->SetNumberField(TEXT("remaining_errors"), System->IsReadyToRun() ? 0 : 1);
    Data->SetBoolField(TEXT("saved"), bSaved);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
