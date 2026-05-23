#include "UeNodeNexusBridgeNiagaraPropertyListFormats.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraIndexedFormat.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

#include <initializer_list>

namespace UeNodeNexusBridge::NiagaraPropertyListFormats
{
bool IsSupportedFormat(const FString& Format)
{
    return Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("full"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
}

TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara property format: %s"), *Format)));
    return Response;
}

void AddObjectIdentity(UObject* Object, TSharedPtr<FJsonObject> Data)
{
    Data->SetStringField(TEXT("object_path"), Object ? Object->GetPathName() : FString());
    Data->SetStringField(TEXT("object_name"), Object ? Object->GetName() : FString());
    Data->SetStringField(TEXT("class_path"), Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString());
}

static void AddColumns(TSharedPtr<FJsonObject> Data, std::initializer_list<const TCHAR*> Columns)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const TCHAR* Column : Columns)
    {
        Values.Add(MakeShared<FJsonValueString>(FString(Column)));
    }
    Data->SetArrayField(TEXT("columns"), Values);
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

static TSharedPtr<FJsonValue> MakePropertyIndexRow(FProperty* Property)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Property ? Property->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Property ? Property->GetClass()->GetName() : FString()));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonValue> MakePropertyCompactRow(UObject* Object, FProperty* Property)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Property ? Property->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Property ? Property->GetClass()->GetName() : FString()));
    Row.Add(PropertyValueToJson(Object, Property));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> BuildIndexedData(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Rows, const FString& FormatName, bool bIncludeTypes)
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

        if (bIncludeTypes)
        {
            EncodedRows.Add(FString::Printf(
                TEXT("%s:%d"),
                *NiagaraIndexedFormat::EscapeToken(Row[0]->AsString()),
                NiagaraIndexedFormat::DictIndex(TypeDict, Types, Row[1]->AsString())));
        }
        else
        {
            EncodedRows.Add(NiagaraIndexedFormat::EscapeToken(Row[0]->AsString()));
        }
    }

    FString Text = FString::Printf(TEXT("G:%s|niagara|properties|%d\n"), *NiagaraIndexedFormat::EscapeToken(Object ? Object->GetPathName() : FString()), EncodedRows.Num());
    if (bIncludeTypes)
    {
        Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("T:"), Types);
    }
    Text += TEXT("P:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), FormatName);
    Data->SetNumberField(TEXT("count"), EncodedRows.Num());
    Data->SetBoolField(TEXT("types_omitted"), !bIncludeTypes);
    if (bIncludeTypes)
    {
        Data->SetNumberField(TEXT("type_count"), Types.Num());
    }
    Data->SetBoolField(TEXT("value_omitted"), true);
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildTinyData(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Rows, const FString& FormatName)
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

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    AddObjectIdentity(Object, Data);
    Data->SetStringField(TEXT("format"), FormatName);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("value_omitted"), true);
    return Data;
}

TSharedPtr<FJsonObject> BuildObjectPropertiesData(
    UObject* Object,
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Format,
    const FString& CompactFormatName,
    const FString& IndexedFormatName,
    const FString& TinyFormatName)
{
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);

    TArray<FString> PropertyNames;
    CollectPropertyNames(Payload, PropertyNames);

    bool bIncludeNonEditable = false;
    Payload->TryGetBoolField(TEXT("include_non_editable"), bIncludeNonEditable);
    bool bIncludeTypes = false;
    Payload->TryGetBoolField(TEXT("include_types"), bIncludeTypes);

    TArray<TSharedPtr<FJsonValue>> Items;
    TArray<TSharedPtr<FJsonValue>> Rows;
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
        else if (bCompact)
        {
            Items.Add(MakePropertyCompactRow(Object, Property));
        }
        else
        {
            Rows.Add(MakePropertyIndexRow(Property));
        }
    }

    if (bIndexed)
    {
        return BuildIndexedData(Object, Rows, IndexedFormatName, bIncludeTypes);
    }
    if (bTiny)
    {
        return BuildTinyData(Object, Rows, TinyFormatName);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    AddObjectIdentity(Object, Data);
    if (!bFull)
    {
        Data->SetStringField(TEXT("format"), CompactFormatName);
        AddColumns(Data, { TEXT("name"), TEXT("type"), TEXT("value") });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    return Data;
}
}
