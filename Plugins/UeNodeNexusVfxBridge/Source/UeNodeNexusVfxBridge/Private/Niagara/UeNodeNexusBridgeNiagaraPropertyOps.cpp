#include "UeNodeNexusNiagaraOps.h"

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

static bool ApplyProperties(UObject* Object, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject> Data, bool bIncludeItems)
{
    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || Params == nullptr)
    {
        Data->SetStringField(TEXT("error"), TEXT("params must be an array"));
        return false;
    }

    bool bDryRun = true;
    bool bAllowNonEditable = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("allow_non_editable"), bAllowNonEditable);

    int32 Planned = 0;
    int32 Changed = 0;
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const TSharedPtr<FJsonValue>& ParamValue : *Params)
    {
        TSharedPtr<FJsonObject> Param = ParamValue->AsObject();
        FString Name;
        if (!Param.IsValid() || !Param->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
        {
            continue;
        }
        ++Planned;
        FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*Name));
        FString ValueText;
        FString Error;
        bool bApplied = false;
        if (Property == nullptr)
        {
            Error = TEXT("property_not_found");
        }
        else if (!ShouldExposeProperty(Property, bAllowNonEditable))
        {
            Error = TEXT("property_not_editable");
        }
        else
        {
            TSharedPtr<FJsonValue> RawValue = Param->TryGetField(TEXT("value"));
            if (!Param->TryGetStringField(TEXT("value_text"), ValueText) && RawValue.IsValid())
            {
                JsonValueToPropertyImportText(Property, RawValue, ValueText, Error);
            }
            if (!bDryRun)
            {
                Object->Modify();
                bApplied = RawValue.IsValid() && !Param->HasField(TEXT("value_text")) ? ApplyPropertyJsonValue(Object, Property, RawValue, ValueText, Error) : ApplyPropertyText(Object, Property, ValueText);
                if (bApplied)
                {
                    Object->PostEditChange();
                    Object->MarkPackageDirty();
                    ++Changed;
                }
                else if (Error.IsEmpty())
                {
                    Error = TEXT("import_text_failed");
                }
            }
        }

        if (bIncludeItems)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), Name);
            Item->SetBoolField(TEXT("found"), Property != nullptr);
            Item->SetBoolField(TEXT("applied"), bApplied);
            Item->SetStringField(TEXT("value_text"), ValueText);
            Item->SetStringField(TEXT("error"), Error);
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);
    Data->SetBoolField(TEXT("details_omitted"), !bIncludeItems);
    if (bIncludeItems)
    {
        Data->SetArrayField(TEXT("items"), Items);
    }
    return true;
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
    if (!ApplyProperties(System, Payload, Data, bFull))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), Data->GetStringField(TEXT("error"))));
        return Response;
    }
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);
    const bool bSaved = bSave && Data->GetBoolField(TEXT("changed")) && SaveAssetPackage(System);
    Data->SetBoolField(TEXT("saved"), bSaved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
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
    if (!ApplyProperties(Renderer, Payload, Data, bFull))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), Data->GetStringField(TEXT("error"))));
        return Response;
    }
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("save"), bSave);
    const bool bSaved = bSave && Data->GetBoolField(TEXT("changed")) && SaveAssetPackage(System);
    Data->SetBoolField(TEXT("saved"), bSaved);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
