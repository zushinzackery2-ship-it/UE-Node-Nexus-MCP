#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Formats/UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonValue.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UeNodeNexusBridgeJson.h"

#include <initializer_list>

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

static TSharedPtr<FJsonValue> MakeParamIndexRow(const FNiagaraVariable& Variable)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));
    Row.Add(MakeShared<FJsonValueString>(Variable.GetType().GetName()));
    return MakeShared<FJsonValueArray>(Row);
}

static bool IsUserParamFormatSupported(const FString& Format)
{
    return Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("full"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
}

static TSharedPtr<FJsonObject> MakeInvalidUserParamFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara user params format: %s"), *Format)));
    return Response;
}

static TSharedPtr<FJsonObject> BuildUserParamsIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TMap<FString, int32> TypeDict;
    TArray<FString> Types;
    TArray<FString> EncodedRows;

    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 2)
        {
            continue;
        }

        EncodedRows.Add(FString::Printf(
            TEXT("%s:%d"),
            *NiagaraIndexedFormat::EscapeToken(Row[0]->AsString()),
            NiagaraIndexedFormat::DictIndex(TypeDict, Types, Row[1]->AsString())));
    }

    FString Text = FString::Printf(TEXT("G:%s|niagara|user_params|%d\n"), *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()), EncodedRows.Num());
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("T:"), Types);
    Text += TEXT("P:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("niagara_user_params_indexed"));
    Data->SetNumberField(TEXT("count"), EncodedRows.Num());
    Data->SetNumberField(TEXT("type_count"), Types.Num());
    Data->SetBoolField(TEXT("value_omitted"), true);
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildUserParamsTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 2)
        {
            continue;
        }

        Items.Add(MakeShared<FJsonValueString>(Row[0]->AsString() + TEXT(" ") + Row[1]->AsString()));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_user_params_tiny"));
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("value_omitted"), true);
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    return Data;
}

TSharedPtr<FJsonObject> HandleNiagaraUserParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!IsUserParamFormatSupported(Format))
    {
        return MakeInvalidUserParamFormatResponse(Operation, RequestId, Format);
    }

    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);

    TArray<FNiagaraVariable> Params;
    System->GetExposedParameters().GetParameters(Params);
    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FNiagaraVariable& Param : Params)
    {
        if (bFull)
        {
            Items.Add(MakeShared<FJsonValueObject>(MakeParamJson(System->GetExposedParameters(), Param)));
        }
        else if (bCompact)
        {
            Items.Add(MakeParamRow(System->GetExposedParameters(), Param));
        }
        else
        {
            Rows.Add(MakeParamIndexRow(Param));
        }
    }

    TSharedPtr<FJsonObject> Data;
    if (bIndexed)
    {
        Data = BuildUserParamsIndexedData(System, Rows);
    }
    else if (bTiny)
    {
        Data = BuildUserParamsTinyData(System, Rows);
    }
    else
    {
        Data = MakeNiagaraAssetData(System);
        Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_user_params_compact") : TEXT("full"));
        if (bCompact)
        {
            AddColumns(Data, { TEXT("name"), TEXT("type"), TEXT("value") });
        }
        Data->SetArrayField(TEXT("items"), Items);
        Data->SetNumberField(TEXT("count"), Items.Num());
        Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    TArray<TSharedPtr<FJsonValue>> Warnings;
    AppendNiagaraEmptySystemWarning(System, Warnings);
    if (Warnings.Num() > 0)
    {
        Response->SetArrayField(TEXT("warnings"), Warnings);
    }
    return Response;
}
}
