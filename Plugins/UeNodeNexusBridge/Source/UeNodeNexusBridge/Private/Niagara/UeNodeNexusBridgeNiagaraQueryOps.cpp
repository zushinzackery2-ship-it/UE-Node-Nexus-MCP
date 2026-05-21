#include "UeNodeNexusBridgeOperations.h"

#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "Dom/JsonValue.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static void AddColumns(TSharedPtr<FJsonObject> Data, std::initializer_list<const TCHAR*> Columns)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const TCHAR* Column : Columns)
    {
        Values.Add(MakeShared<FJsonValueString>(FString(Column)));
    }
    Data->SetArrayField(TEXT("columns"), Values);
}

static TSharedPtr<FJsonValue> MakeVector2Value(const FVector2f& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("x"), Value.X);
    Json->SetNumberField(TEXT("y"), Value.Y);
    return MakeShared<FJsonValueObject>(Json);
}

static TSharedPtr<FJsonValue> MakeVector3Value(const FVector3f& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("x"), Value.X);
    Json->SetNumberField(TEXT("y"), Value.Y);
    Json->SetNumberField(TEXT("z"), Value.Z);
    return MakeShared<FJsonValueObject>(Json);
}

static TSharedPtr<FJsonValue> MakeVector4Value(const FVector4f& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("x"), Value.X);
    Json->SetNumberField(TEXT("y"), Value.Y);
    Json->SetNumberField(TEXT("z"), Value.Z);
    Json->SetNumberField(TEXT("w"), Value.W);
    return MakeShared<FJsonValueObject>(Json);
}

static TSharedPtr<FJsonValue> MakeColorValue(const FLinearColor& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("r"), Value.R);
    Json->SetNumberField(TEXT("g"), Value.G);
    Json->SetNumberField(TEXT("b"), Value.B);
    Json->SetNumberField(TEXT("a"), Value.A);
    return MakeShared<FJsonValueObject>(Json);
}

static TSharedPtr<FJsonValue> MakeParamValue(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        return MakeShared<FJsonValueNumber>(Store.GetParameterValue<float>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        return MakeShared<FJsonValueNumber>(Store.GetParameterValue<int32>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        return MakeShared<FJsonValueBoolean>(Store.GetParameterValue<FNiagaraBool>(Variable).GetValue());
    }
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        return MakeVector2Value(Store.GetParameterValue<FVector2f>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def())
    {
        return MakeVector3Value(Store.GetParameterValue<FVector3f>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        return MakeVector4Value(Store.GetParameterValue<FVector4f>(Variable));
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        return MakeColorValue(Store.GetParameterValue<FLinearColor>(Variable));
    }
    if (Type.GetClass() != nullptr)
    {
        UObject* Object = Store.GetUObject(Variable);
        return MakeShared<FJsonValueString>(Object ? Object->GetPathName() : FString());
    }
    return MakeShared<FJsonValueString>(TEXT("<unsupported>"));
}

static TSharedPtr<FJsonObject> MakeParamJson(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Variable.GetName().ToString());
    Json->SetStringField(TEXT("type"), Variable.GetType().GetName());
    Json->SetField(TEXT("value"), MakeParamValue(Store, Variable));
    return Json;
}

static TSharedPtr<FJsonValue> MakeParamRow(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));
    Row.Add(MakeShared<FJsonValueString>(Variable.GetType().GetName()));
    Row.Add(MakeParamValue(Store, Variable));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> HandleNiagaraSystemSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    int32 RendererCount = 0;
    TArray<TSharedPtr<FJsonValue>> Emitters;
    for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
    {
        Emitters.Add(MakeNiagaraEmitterRow(System, Index));
        const FVersionedNiagaraEmitterData* EmitterData = System->GetEmitterHandles()[Index].GetEmitterData();
        RendererCount += EmitterData ? EmitterData->GetRenderers().Num() : 0;
    }

    TArray<FNiagaraVariable> Params;
    System->GetExposedParameters().GetParameters(Params);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    Data->SetNumberField(TEXT("renderer_count"), RendererCount);
    Data->SetNumberField(TEXT("user_param_count"), Params.Num());
    Data->SetBoolField(TEXT("ready_to_run"), System->IsReadyToRun());
    Data->SetBoolField(TEXT("needs_compile"), System->NeedsRequestCompile());
    Data->SetStringField(TEXT("format"), TEXT("niagara_system_summary_compact"));
    AddColumns(Data, { TEXT("index"), TEXT("name"), TEXT("enabled"), TEXT("renderer_count") });
    Data->SetArrayField(TEXT("emitters"), Emitters);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraEmittersList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
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
    for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
    {
        Items.Add(bCompact ? MakeNiagaraEmitterRow(System, Index) : MakeShared<FJsonValueObject>(MakeNiagaraEmitterJson(System, Index)));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_emitters_compact") : TEXT("full"));
    if (bCompact)
    {
        AddColumns(Data, { TEXT("index"), TEXT("name"), TEXT("enabled"), TEXT("renderer_count") });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleNiagaraUserParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
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

    TArray<FNiagaraVariable> Params;
    System->GetExposedParameters().GetParameters(Params);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const FNiagaraVariable& Param : Params)
    {
        Items.Add(bCompact ? MakeParamRow(System->GetExposedParameters(), Param) : MakeShared<FJsonValueObject>(MakeParamJson(System->GetExposedParameters(), Param)));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_user_params_compact") : TEXT("full"));
    if (bCompact)
    {
        AddColumns(Data, { TEXT("name"), TEXT("type"), TEXT("value") });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
