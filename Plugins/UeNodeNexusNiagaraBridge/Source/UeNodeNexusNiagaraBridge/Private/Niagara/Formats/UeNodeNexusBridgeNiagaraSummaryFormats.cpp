#include "UeNodeNexusBridgeNiagaraSummaryFormats.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonValue.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

#include <initializer_list>

namespace UeNodeNexusBridge::NiagaraSummaryFormats
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
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara summary format: %s"), *Format)));
    return Response;
}

static TArray<TSharedPtr<FJsonValue>> BuildEmitterRows(UNiagaraSystem* System)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    if (System == nullptr)
    {
        return Rows;
    }
    for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
    {
        Rows.Add(MakeNiagaraEmitterRow(System, Index));
    }
    return Rows;
}

static TArray<TSharedPtr<FJsonValue>> BuildEmitterObjects(UNiagaraSystem* System)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    if (System == nullptr)
    {
        return Items;
    }
    for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
    {
        Items.Add(MakeShared<FJsonValueObject>(MakeNiagaraEmitterJson(System, Index)));
    }
    return Items;
}

static int32 CountUserParams(UNiagaraSystem* System)
{
    TArray<FNiagaraVariable> Params;
    if (System != nullptr)
    {
        System->GetExposedParameters().GetParameters(Params);
    }
    return Params.Num();
}

static void AddSummaryCounts(UNiagaraSystem* System, TSharedPtr<FJsonObject> Data)
{
    Data->SetNumberField(TEXT("emitter_count"), System ? System->GetEmitterHandles().Num() : 0);
    Data->SetNumberField(TEXT("enabled_emitter_count"), CountEnabledNiagaraEmitters(System));
    Data->SetNumberField(TEXT("renderer_count"), CountNiagaraRenderers(System));
    Data->SetNumberField(TEXT("enabled_renderer_count"), CountEnabledNiagaraRenderers(System));
    Data->SetNumberField(TEXT("user_param_count"), CountUserParams(System));
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    Data->SetBoolField(TEXT("ready_to_run"), System ? System->IsReadyToRun() : false);
    Data->SetBoolField(TEXT("needs_compile"), System ? System->NeedsRequestCompile() : false);
    Data->SetNumberField(TEXT("readiness_issue_count"), CountNiagaraReadinessIssues(System));
}

static void EncodeEmitterRows(const TArray<TSharedPtr<FJsonValue>>& Rows, TArray<FString>& OutNames, TArray<FString>& OutRows, int32& OutEnabledCount, int32& OutRendererCount)
{
    TMap<FString, int32> NameDict;
    OutEnabledCount = 0;
    OutRendererCount = 0;

    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 4)
        {
            continue;
        }

        const bool bEnabled = Row[2]->AsBool();
        const int32 RowRendererCount = static_cast<int32>(Row[3]->AsNumber());
        OutEnabledCount += bEnabled ? 1 : 0;
        OutRendererCount += RowRendererCount;
        OutRows.Add(FString::Printf(
            TEXT("%d:%d:%d:%d"),
            static_cast<int32>(Row[0]->AsNumber()),
            NiagaraIndexedFormat::DictIndex(NameDict, OutNames, Row[1]->AsString()),
            bEnabled ? 1 : 0,
            RowRendererCount));
    }
}

static TSharedPtr<FJsonObject> BuildEmitterIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows, const FString& GroupName, const FString& FormatName)
{
    TArray<FString> Names;
    TArray<FString> EncodedRows;
    int32 EnabledCount = 0;
    int32 RendererCount = 0;
    EncodeEmitterRows(Rows, Names, EncodedRows, EnabledCount, RendererCount);

    FString Text = FString::Printf(TEXT("G:%s|niagara|%s|%d\n"), *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()), *GroupName, EncodedRows.Num());
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("N:"), Names);
    Text += TEXT("E:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), FormatName);
    Data->SetNumberField(TEXT("count"), EncodedRows.Num());
    Data->SetNumberField(TEXT("enabled_count"), EnabledCount);
    Data->SetNumberField(TEXT("renderer_count"), RendererCount);
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

static TSharedPtr<FJsonObject> BuildEmitterTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows, const FString& FormatName)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    int32 EnabledCount = 0;
    int32 RendererCount = 0;

    for (const TSharedPtr<FJsonValue>& Value : Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>& Row = Value->AsArray();
        if (Row.Num() < 4)
        {
            continue;
        }

        const bool bEnabled = Row[2]->AsBool();
        const int32 RowRendererCount = static_cast<int32>(Row[3]->AsNumber());
        EnabledCount += bEnabled ? 1 : 0;
        RendererCount += RowRendererCount;
        Items.Add(MakeShared<FJsonValueString>(FString::Printf(
            TEXT("E%d %s%s r=%d"),
            static_cast<int32>(Row[0]->AsNumber()),
            *Row[1]->AsString(),
            bEnabled ? TEXT("") : TEXT("(off)"),
            RowRendererCount)));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), FormatName);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetNumberField(TEXT("enabled_count"), EnabledCount);
    Data->SetNumberField(TEXT("renderer_count"), RendererCount);
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    return Data;
}

static TSharedPtr<FJsonObject> BuildSystemSummaryIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TArray<FString> Names;
    TArray<FString> EncodedRows;
    int32 EnabledCount = 0;
    int32 RendererCount = 0;
    EncodeEmitterRows(Rows, Names, EncodedRows, EnabledCount, RendererCount);

    FString Text = FString::Printf(
        TEXT("G:%s|niagara|summary|%d\n"),
        *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()),
        EncodedRows.Num());
    Text += FString::Printf(
        TEXT("S:ready=%d;needs_compile=%d;emitters=%d;enabled_emitters=%d;renderers=%d;enabled_renderers=%d;user_params=%d;readiness_issues=%d\n"),
        System && System->IsReadyToRun() ? 1 : 0,
        System && System->NeedsRequestCompile() ? 1 : 0,
        System ? System->GetEmitterHandles().Num() : 0,
        CountEnabledNiagaraEmitters(System),
        CountNiagaraRenderers(System),
        CountEnabledNiagaraRenderers(System),
        CountUserParams(System),
        CountNiagaraReadinessIssues(System));
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("N:"), Names);
    Text += TEXT("E:") + FString::Join(EncodedRows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), TEXT("niagara_system_summary_indexed"));
    AddSummaryCounts(System, Data);
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

TSharedPtr<FJsonObject> BuildSystemSummaryData(UNiagaraSystem* System, const FString& Format)
{
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
    const TArray<TSharedPtr<FJsonValue>> Rows = BuildEmitterRows(System);

    if (bIndexed)
    {
        return BuildSystemSummaryIndexedData(System, Rows);
    }
    if (bTiny)
    {
        TSharedPtr<FJsonObject> Data = BuildEmitterTinyData(System, Rows, TEXT("niagara_system_summary_tiny"));
        AddSummaryCounts(System, Data);
        return Data;
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_system_summary_compact") : TEXT("full"));
    AddSummaryCounts(System, Data);
    if (bCompact)
    {
        AddColumns(Data, { TEXT("index"), TEXT("name"), TEXT("enabled"), TEXT("renderer_count") });
        Data->SetArrayField(TEXT("emitters"), Rows);
    }
    else if (bFull)
    {
        Data->SetArrayField(TEXT("emitters"), BuildEmitterObjects(System));
    }
    return Data;
}

TSharedPtr<FJsonObject> BuildEmittersListData(UNiagaraSystem* System, const FString& Format)
{
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bCompact = Format.Equals(TEXT("compact"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bTiny = Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase);
    const TArray<TSharedPtr<FJsonValue>> Rows = BuildEmitterRows(System);

    if (bIndexed)
    {
        return BuildEmitterIndexedData(System, Rows, TEXT("emitters"), TEXT("niagara_emitters_indexed"));
    }
    if (bTiny)
    {
        return BuildEmitterTinyData(System, Rows, TEXT("niagara_emitters_tiny"));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetData(System);
    Data->SetStringField(TEXT("format"), bCompact ? TEXT("niagara_emitters_compact") : TEXT("full"));
    if (bCompact)
    {
        AddColumns(Data, { TEXT("index"), TEXT("name"), TEXT("enabled"), TEXT("renderer_count") });
        Data->SetArrayField(TEXT("items"), Rows);
    }
    else if (bFull)
    {
        Data->SetArrayField(TEXT("items"), BuildEmitterObjects(System));
    }
    Data->SetNumberField(TEXT("count"), Rows.Num());
    Data->SetBoolField(TEXT("has_emitter_stack"), NiagaraSystemHasEmitterStack(System));
    return Data;
}
}
