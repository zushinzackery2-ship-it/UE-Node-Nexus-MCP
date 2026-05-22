#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"
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

static FNiagaraEmitterHandle* ResolveEmitterHandle(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    int32 EmitterIndex = INDEX_NONE;
    if (!ReadIndex(Payload, TEXT("emitter_index"), EmitterIndex) || !System->GetEmitterHandles().IsValidIndex(EmitterIndex))
    {
        OutError = MakeEnvelope(Operation, RequestId, false);
        OutError->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_emitter_index"), TEXT("emitter_index is required and must point to an existing emitter")));
        return nullptr;
    }
    return &System->GetEmitterHandles()[EmitterIndex];
}
static UNiagaraRendererProperties* ResolveRenderer(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    FNiagaraEmitterHandle* Handle = ResolveEmitterHandle(System, Payload, OutError, Operation, RequestId);
    if (Handle == nullptr)
    {
        return nullptr;
    }
    int32 RendererIndex = INDEX_NONE;
    if (!ReadIndex(Payload, TEXT("renderer_index"), RendererIndex))
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
static void AddObjectIdentity(UObject* Object, TSharedPtr<FJsonObject> Data)
{
    Data->SetStringField(TEXT("object_path"), Object ? Object->GetPathName() : FString());
    Data->SetStringField(TEXT("object_name"), Object ? Object->GetName() : FString());
    Data->SetStringField(TEXT("class_path"), Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString());
}
static void CollectPropertyNames(const TSharedPtr<FJsonObject>& Payload, TArray<FString>& OutPropertyNames)
{
    Payload->TryGetStringArrayField(TEXT("property_names"), OutPropertyNames);
    OutPropertyNames.RemoveAll([](const FString& Name)
    {
        return Name.IsEmpty();
    });
}
static bool PropertyNameMatches(FProperty* Property, const TArray<FString>& PropertyNames)
{
    if (PropertyNames.Num() == 0)
    {
        return true;
    }
    for (const FString& Name : PropertyNames)
    {
        if (Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

static TSharedPtr<FJsonObject> MakeObjectPropertiesData(UObject* Object, const TSharedPtr<FJsonObject>& Payload, const FString& FormatName)
{
    TArray<FString> PropertyNames;
    CollectPropertyNames(Payload, PropertyNames);
    bool bIncludeNonEditable = false;
    FString Format = TEXT("compact");
    Payload->TryGetBoolField(TEXT("include_non_editable"), bIncludeNonEditable);
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!ShouldExposeProperty(Property, bIncludeNonEditable) || !PropertyNameMatches(Property, PropertyNames))
        {
            continue;
        }
        if (bFull)
        {
            Items.Add(MakeShared<FJsonValueObject>(PropertyToJson(Object, Property, true)));
        }
        else
        {
            TArray<TSharedPtr<FJsonValue>> Row;
            Row.Add(MakeShared<FJsonValueString>(Property->GetName()));
            Row.Add(MakeShared<FJsonValueString>(Property->GetClass()->GetName()));
            Row.Add(PropertyValueToJson(Object, Property));
            Items.Add(MakeShared<FJsonValueArray>(Row));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    AddObjectIdentity(Object, Data);
    if (!bFull)
    {
        Data->SetStringField(TEXT("format"), FormatName);
        Data->SetArrayField(TEXT("columns"), { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("type")), MakeShared<FJsonValueString>(TEXT("value")) });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    return Data;
}

static bool ApplyProperties(UObject* Object, const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject> Data)
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

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Name);
        Item->SetBoolField(TEXT("found"), Property != nullptr);
        Item->SetBoolField(TEXT("applied"), bApplied);
        Item->SetStringField(TEXT("value_text"), ValueText);
        Item->SetStringField(TEXT("error"), Error);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }

    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);
    Data->SetArrayField(TEXT("items"), Items);
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

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeObjectPropertiesData(System, Payload, TEXT("niagara_system_properties_compact")));
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

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    if (!ApplyProperties(System, Payload, Data))
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
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), MakeObjectPropertiesData(Renderer, Payload, TEXT("niagara_renderer_properties_compact")));
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
    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    AddObjectIdentity(Renderer, Data);
    if (!ApplyProperties(Renderer, Payload, Data))
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
