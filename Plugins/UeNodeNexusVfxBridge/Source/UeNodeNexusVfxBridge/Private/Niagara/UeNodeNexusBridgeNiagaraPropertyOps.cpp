#include "UeNodeNexusNiagaraOps.h"
#include "Properties/NiagaraPropertyWrite.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Formats/UeNodeNexusBridgeNiagaraPropertyListFormats.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
namespace UeNodeNexusBridge
{
static UNiagaraRendererProperties* ResolveRenderer(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    FNiagaraEmitterHandle* Handle = ResolveNiagaraEmitterHandle(System, Payload, OutError, Operation, RequestId);
    if (Handle == nullptr)
    {
        return nullptr;
    }
    int32 RendererIndex = INDEX_NONE;
    if (!ReadNiagaraIndexField(Payload, TEXT("renderer_index"), RendererIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_renderer_index"), TEXT("renderer_index is required")));
        return nullptr;
    }
    FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
    if (EmitterData == nullptr || !EmitterData->GetRenderers().IsValidIndex(RendererIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("renderer_not_found"), TEXT("renderer_index does not point to an existing renderer")));
        return nullptr;
    }
    return EmitterData->GetRenderers()[RendererIndex];
}

static bool ReadWriteFormat(const TSharedPtr<FJsonObject>& Payload, FString& OutFormat)
{
    OutFormat = TEXT("summary");
    Payload->TryGetStringField(TEXT("format"), OutFormat);
    return OutFormat.Equals(TEXT("summary"), ESearchCase::IgnoreCase) || OutFormat.Equals(TEXT("full"), ESearchCase::IgnoreCase);
}

TSharedPtr<FJsonObject> HandleNiagaraSystemPropertiesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!NiagaraPropertyListFormats::IsSupportedFormat(Format))
    {
        return NiagaraPropertyListFormats::MakeInvalidFormatResponse(Operation, RequestId, Format);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), NiagaraPropertyListFormats::BuildObjectPropertiesData(
        System,
        Payload,
        Format,
        TEXT("niagara_system_properties_compact"),
        TEXT("niagara_system_properties_indexed"),
        TEXT("niagara_system_properties_tiny")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraSystemPropertiesSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Format;
    if (!ReadWriteFormat(Payload, Format))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), TEXT("format must be summary or full")));
        return Response;
    }

    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    TSharedPtr<FJsonObject> Data = bFull ? MakeNiagaraAssetData(System) : MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), bFull ? TEXT("niagara_system_properties_set_full") : TEXT("niagara_system_properties_set_summary"));
    return WriteNiagaraProperties(Operation, RequestId, System, System, Payload, Data, bFull);
}

TSharedPtr<FJsonObject> HandleNiagaraRendererPropertiesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }
    UNiagaraRendererProperties* Renderer = ResolveRenderer(System, Payload, EarlyResponse, Operation, RequestId);
    if (Renderer == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!NiagaraPropertyListFormats::IsSupportedFormat(Format))
    {
        return NiagaraPropertyListFormats::MakeInvalidFormatResponse(Operation, RequestId, Format);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), NiagaraPropertyListFormats::BuildObjectPropertiesData(
        Renderer,
        Payload,
        Format,
        TEXT("niagara_renderer_properties_compact"),
        TEXT("niagara_renderer_properties_indexed"),
        TEXT("niagara_renderer_properties_tiny")));
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraRendererPropertiesSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }
    UNiagaraRendererProperties* Renderer = ResolveRenderer(System, Payload, EarlyResponse, Operation, RequestId);
    if (Renderer == nullptr)
    {
        return EarlyResponse;
    }
    FString Format;
    if (!ReadWriteFormat(Payload, Format))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), TEXT("format must be summary or full")));
        return Response;
    }

    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    TSharedPtr<FJsonObject> Data = bFull ? MakeNiagaraAssetData(System) : MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), bFull ? TEXT("niagara_renderer_properties_set_full") : TEXT("niagara_renderer_properties_set_summary"));
    if (bFull)
    {
        NiagaraPropertyListFormats::AddObjectIdentity(Renderer, Data);
    }
    return WriteNiagaraProperties(Operation, RequestId, Renderer, System, Payload, Data, bFull);
}
}
