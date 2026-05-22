#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "Dom/JsonValue.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraDecalRendererProperties.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterFactoryNew.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraVolumeRendererProperties.h"

namespace UeNodeNexusBridge
{
static bool ReadIndex(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, int32& OutValue)
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

static FNiagaraEmitterHandle* FindEmitterById(UNiagaraSystem* System, const FGuid& Id)
{
    for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        if (Handle.GetId() == Id)
        {
            return &Handle;
        }
    }
    return nullptr;
}

static FNiagaraEmitterHandle* ResolveEmitterByIndex(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    int32 EmitterIndex = INDEX_NONE;
    if (!ReadIndex(Payload, TEXT("emitter_index"), EmitterIndex) || !System->GetEmitterHandles().IsValidIndex(EmitterIndex))
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_emitter_index"), TEXT("emitter_index is required and must point to an existing emitter")));
        return nullptr;
    }
    return &System->GetEmitterHandles()[EmitterIndex];
}

static UNiagaraRendererProperties* NewRendererForType(UObject* Outer, const FString& Type)
{
    if (Type.Equals(TEXT("sprite"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraSpriteRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("ribbon"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraRibbonRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("mesh"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraMeshRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("light"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraLightRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("component"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraComponentRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("decal"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraDecalRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    if (Type.Equals(TEXT("volume"), ESearchCase::IgnoreCase))
    {
        return NewObject<UNiagaraVolumeRendererProperties>(Outer, NAME_None, RF_Transactional);
    }
    return nullptr;
}

static TSharedPtr<FJsonObject> RendererToJson(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("emitter_index"), EmitterIndex);
    Json->SetNumberField(TEXT("renderer_index"), RendererIndex);
    Json->SetStringField(TEXT("object_path"), Renderer ? Renderer->GetPathName() : FString());
    Json->SetStringField(TEXT("class"), Renderer ? Renderer->GetClass()->GetName() : FString());
    Json->SetBoolField(TEXT("enabled"), Renderer ? Renderer->GetIsEnabled() : false);
    Json->SetStringField(TEXT("material_path"), GetRendererMaterialPath(Renderer, 0));
    return Json;
}

static TSharedPtr<FJsonValue> RendererToRow(int32 EmitterIndex, int32 RendererIndex, UNiagaraRendererProperties* Renderer)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueNumber>(EmitterIndex));
    Row.Add(MakeShared<FJsonValueNumber>(RendererIndex));
    Row.Add(MakeShared<FJsonValueString>(Renderer ? Renderer->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueBoolean>(Renderer ? Renderer->GetIsEnabled() : false));
    Row.Add(MakeShared<FJsonValueString>(GetRendererMaterialPath(Renderer, 0)));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> HandleNiagaraEmitterCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Mode = TEXT("default");
    FString SourceEmitterPath;
    FString Name;
    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetStringField(TEXT("mode"), Mode);
    Payload->TryGetStringField(TEXT("source_emitter_path"), SourceEmitterPath);
    Payload->TryGetStringField(TEXT("name"), Name);
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
        Data->SetBoolField(TEXT("dry_run"), true);
        Data->SetBoolField(TEXT("changed"), false);
        Data->SetStringField(TEXT("mode"), Mode);
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    UNiagaraEmitter* Emitter = nullptr;
    bool bCreateCopy = true;
    if (!SourceEmitterPath.IsEmpty() || Mode.Equals(TEXT("from_asset"), ESearchCase::IgnoreCase))
    {
        Emitter = LoadObject<UNiagaraEmitter>(nullptr, *SourceEmitterPath);
        if (Emitter == nullptr)
        {
            TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
            Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("source_emitter_not_found"), TEXT("source_emitter_path could not be loaded")));
            return Response;
        }
    }
    else
    {
        const bool bDefaultModulesAndRenderers = !Mode.Equals(TEXT("empty"), ESearchCase::IgnoreCase);
        Emitter = NewObject<UNiagaraEmitter>(GetTransientPackage(), NAME_None, RF_Transactional);
        UNiagaraEmitterFactoryNew::InitializeEmitter(Emitter, bDefaultModulesAndRenderers);
        Emitter->SetUniqueEmitterName(Name.IsEmpty() ? TEXT("MCPEmitter") : Name);
        bCreateCopy = true;
    }

    const FGuid Version = Emitter->GetExposedVersion().VersionGuid;
    const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Emitter, Version, bCreateCopy);
    FNiagaraEmitterHandle* Handle = FindEmitterById(System, HandleId);
    if (Handle != nullptr && !Name.IsEmpty())
    {
        Handle->SetName(FName(*Name), *System);
    }
    System->Modify();
    System->MarkPackageDirty();
    System->PostEditChange();
    const bool bSaved = bSave && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetBoolField(TEXT("dry_run"), false);
    Data->SetBoolField(TEXT("changed"), Handle != nullptr);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetStringField(TEXT("handle_id"), HandleId.ToString(EGuidFormats::DigitsWithHyphens));
    Data->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    Data->SetNumberField(TEXT("renderer_count"), CountNiagaraRenderers(System));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Handle != nullptr);
    Response->SetObjectField(TEXT("data"), Data);
    if (Handle == nullptr)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("emitter_add_failed"), TEXT("Niagara editor API did not return a valid emitter handle")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraRenderersList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
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
        FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[EmitterIndex].GetEmitterData();
        if (EmitterData == nullptr)
        {
            continue;
        }
        const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
        for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
        {
            Items.Add(bCompact ? RendererToRow(EmitterIndex, RendererIndex, Renderers[RendererIndex]) : MakeShared<FJsonValueObject>(RendererToJson(EmitterIndex, RendererIndex, Renderers[RendererIndex])));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_renderers_compact") : TEXT("full"));
    if (bCompact)
    {
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("emitter_index")),
            MakeShared<FJsonValueString>(TEXT("renderer_index")),
            MakeShared<FJsonValueString>(TEXT("class")),
            MakeShared<FJsonValueString>(TEXT("enabled")),
            MakeShared<FJsonValueString>(TEXT("material_path"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraRendererCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }
    FNiagaraEmitterHandle* Handle = ResolveEmitterByIndex(System, Payload, EarlyResponse, Operation, RequestId);
    if (Handle == nullptr)
    {
        return EarlyResponse;
    }

    FString Type = TEXT("sprite");
    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetStringField(TEXT("renderer_type"), Type);
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    FVersionedNiagaraEmitter Instance = Handle->GetInstance();
    UNiagaraEmitter* Emitter = Instance.Emitter;
    FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
    if (Emitter == nullptr || EmitterData == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("emitter_data_not_found"), TEXT("Emitter instance data could not be resolved")));
        return Response;
    }

    const int32 BeforeCount = EmitterData->GetRenderers().Num();
    if (!bDryRun)
    {
        UNiagaraRendererProperties* Renderer = NewRendererForType(Emitter, Type);
        if (Renderer == nullptr)
        {
            TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
            Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("unsupported_renderer_type"), TEXT("renderer_type must be sprite, ribbon, mesh, light, component, decal, or volume")));
            return Response;
        }
        Emitter->AddRenderer(Renderer, Instance.Version);
        System->Modify();
        System->MarkPackageDirty();
        System->PostEditChange();
    }
    const int32 AfterCount = EmitterData->GetRenderers().Num();
    const bool bSaved = bSave && AfterCount > BeforeCount && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("changed"), AfterCount > BeforeCount);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetNumberField(TEXT("renderer_count_before"), BeforeCount);
    Data->SetNumberField(TEXT("renderer_count_after"), AfterCount);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
