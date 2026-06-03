#include "Lint/UeNodeNexusBridgeNiagaraAssetLintShared.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Formats/UeNodeNexusBridgeNiagaraIndexedFormat.h"

#include "Dom/JsonValue.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge::NiagaraAssetLint
{
TSharedPtr<FJsonObject> MakeLintIssue(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    int32 EmitterIndex,
    int32 RendererIndex,
    const FString& ModuleUsage,
    int32 ModuleIndex)
{
    TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
    Issue->SetStringField(TEXT("severity"), Severity);
    Issue->SetStringField(TEXT("code"), Code);
    Issue->SetStringField(TEXT("message"), Message);
    if (EmitterIndex != INDEX_NONE)
    {
        Issue->SetNumberField(TEXT("emitter_index"), EmitterIndex);
    }
    if (RendererIndex != INDEX_NONE)
    {
        Issue->SetNumberField(TEXT("renderer_index"), RendererIndex);
    }
    if (!ModuleUsage.IsEmpty())
    {
        Issue->SetStringField(TEXT("usage"), ModuleUsage);
    }
    if (ModuleIndex != INDEX_NONE)
    {
        Issue->SetNumberField(TEXT("module_index"), ModuleIndex);
    }
    return Issue;
}

void AddIssue(TArray<TSharedPtr<FJsonValue>>& Issues, const TSharedPtr<FJsonObject>& Issue)
{
    Issues.Add(MakeShared<FJsonValueObject>(Issue));
}

FLintSeverityCounts CountSeverities(const TArray<TSharedPtr<FJsonValue>>& Issues)
{
    FLintSeverityCounts Counts;
    for (const TSharedPtr<FJsonValue>& IssueValue : Issues)
    {
        const TSharedPtr<FJsonObject>* IssueObject = nullptr;
        if (!IssueValue.IsValid() || !IssueValue->TryGetObject(IssueObject) || !IssueObject || !IssueObject->IsValid())
        {
            ++Counts.OtherCount;
            continue;
        }

        FString Severity;
        (*IssueObject)->TryGetStringField(TEXT("severity"), Severity);
        if (Severity.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Severity.Equals(TEXT("fatal"), ESearchCase::IgnoreCase))
        {
            ++Counts.ErrorCount;
        }
        else if (Severity.Equals(TEXT("warning"), ESearchCase::IgnoreCase))
        {
            ++Counts.WarningCount;
        }
        else if (Severity.Equals(TEXT("info"), ESearchCase::IgnoreCase))
        {
            ++Counts.InfoCount;
        }
        else
        {
            ++Counts.OtherCount;
        }
    }
    return Counts;
}

bool IsSupportedLintFormat(const FString& Format)
{
    return Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase)
        || Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
}

TSharedPtr<FJsonObject> MakeInvalidLintFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_format"), FString::Printf(TEXT("Unsupported Niagara lint format: %s"), *Format)));
    return Response;
}

namespace
{
TSharedPtr<FJsonObject> MakeSeverityCountsJson(const FLintSeverityCounts& Counts)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("error"), Counts.ErrorCount);
    Json->SetNumberField(TEXT("warning"), Counts.WarningCount);
    Json->SetNumberField(TEXT("info"), Counts.InfoCount);
    Json->SetNumberField(TEXT("other"), Counts.OtherCount);
    return Json;
}

FString ShortSeverity(const FString& Severity)
{
    if (Severity.Equals(TEXT("error"), ESearchCase::IgnoreCase) || Severity.Equals(TEXT("fatal"), ESearchCase::IgnoreCase))
    {
        return TEXT("E");
    }
    if (Severity.Equals(TEXT("warning"), ESearchCase::IgnoreCase))
    {
        return TEXT("W");
    }
    if (Severity.Equals(TEXT("info"), ESearchCase::IgnoreCase))
    {
        return TEXT("I");
    }
    return TEXT("O");
}

FString IssueLocation(const TSharedPtr<FJsonObject>& Issue)
{
    if (!Issue.IsValid())
    {
        return TEXT("-");
    }

    FString Location;
    double Number = 0.0;
    if (Issue->TryGetNumberField(TEXT("emitter_index"), Number))
    {
        Location += FString::Printf(TEXT("e%d"), static_cast<int32>(Number));
    }
    if (Issue->TryGetNumberField(TEXT("renderer_index"), Number))
    {
        Location += FString::Printf(TEXT("%sr%d"), Location.IsEmpty() ? TEXT("") : TEXT("/"), static_cast<int32>(Number));
    }
    FString Usage;
    if (Issue->TryGetStringField(TEXT("usage"), Usage) && !Usage.IsEmpty())
    {
        Location += FString::Printf(TEXT("%s%s"), Location.IsEmpty() ? TEXT("") : TEXT("/"), *Usage);
    }
    if (Issue->TryGetNumberField(TEXT("module_index"), Number))
    {
        Location += FString::Printf(TEXT("%sm%d"), Location.IsEmpty() ? TEXT("") : TEXT("/"), static_cast<int32>(Number));
    }
    return Location.IsEmpty() ? FString(TEXT("-")) : Location;
}

TArray<TSharedPtr<FJsonObject>> IssueObjects(const TArray<TSharedPtr<FJsonValue>>& Issues)
{
    TArray<TSharedPtr<FJsonObject>> Objects;
    Objects.Reserve(Issues.Num());
    for (const TSharedPtr<FJsonValue>& IssueValue : Issues)
    {
        const TSharedPtr<FJsonObject>* IssueObject = nullptr;
        if (IssueValue.IsValid() && IssueValue->TryGetObject(IssueObject) && IssueObject != nullptr && IssueObject->IsValid())
        {
            Objects.Add(*IssueObject);
        }
    }
    return Objects;
}
}

void AddLintCounts(TSharedPtr<FJsonObject> Data, const FLintSeverityCounts& SeverityCounts, int32 IssueCount, int32 BlockingIssueCount)
{
    Data->SetNumberField(TEXT("issue_count"), IssueCount);
    Data->SetObjectField(TEXT("severity_counts"), MakeSeverityCountsJson(SeverityCounts));
    Data->SetNumberField(TEXT("error_count"), SeverityCounts.ErrorCount);
    Data->SetNumberField(TEXT("warning_count"), SeverityCounts.WarningCount);
    Data->SetNumberField(TEXT("info_count"), SeverityCounts.InfoCount);
    Data->SetNumberField(TEXT("other_count"), SeverityCounts.OtherCount);
    Data->SetNumberField(TEXT("blocking_issue_count"), BlockingIssueCount);
    Data->SetBoolField(TEXT("passed"), BlockingIssueCount == 0);
    Data->SetBoolField(TEXT("clean"), IssueCount == 0);
}

TSharedPtr<FJsonObject> BuildLintIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Issues, const FLintSeverityCounts& SeverityCounts, int32 BlockingIssueCount)
{
    TMap<FString, int32> CodeDict;
    TArray<FString> Codes;
    TArray<FString> Rows;

    for (const TSharedPtr<FJsonObject>& Issue : IssueObjects(Issues))
    {
        FString Severity;
        FString Code;
        Issue->TryGetStringField(TEXT("severity"), Severity);
        Issue->TryGetStringField(TEXT("code"), Code);
        Rows.Add(FString::Printf(
            TEXT("%s:%d:%s"),
            *ShortSeverity(Severity),
            NiagaraIndexedFormat::DictIndex(CodeDict, Codes, Code),
            *NiagaraIndexedFormat::EscapeToken(IssueLocation(Issue))));
    }

    FString Text = FString::Printf(
        TEXT("G:%s|niagara|lint|%d\n"),
        *NiagaraIndexedFormat::EscapeToken(System ? System->GetPathName() : FString()),
        Issues.Num());
    Text += FString::Printf(
        TEXT("S:e=%d;w=%d;i=%d;o=%d;block=%d;pass=%d\n"),
        SeverityCounts.ErrorCount,
        SeverityCounts.WarningCount,
        SeverityCounts.InfoCount,
        SeverityCounts.OtherCount,
        BlockingIssueCount,
        BlockingIssueCount == 0 ? 1 : 0);
    Text += NiagaraIndexedFormat::JoinDictionaryLine(TEXT("C:"), Codes);
    Text += TEXT("I:") + FString::Join(Rows, TEXT("|")) + TEXT("\n");

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_asset_lint_indexed"));
    AddLintCounts(Data, SeverityCounts, Issues.Num(), BlockingIssueCount);
    NiagaraIndexedFormat::SetTextPayload(Data, Text);
    return Data;
}

TSharedPtr<FJsonObject> BuildLintTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Issues, const FLintSeverityCounts& SeverityCounts, int32 BlockingIssueCount)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    for (const TSharedPtr<FJsonObject>& Issue : IssueObjects(Issues))
    {
        FString Severity;
        FString Code;
        Issue->TryGetStringField(TEXT("severity"), Severity);
        Issue->TryGetStringField(TEXT("code"), Code);
        Items.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s:%s@%s"), *ShortSeverity(Severity), *Code, *IssueLocation(Issue))));
    }

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetStringField(TEXT("format"), TEXT("niagara_asset_lint_tiny"));
    AddLintCounts(Data, SeverityCounts, Issues.Num(), BlockingIssueCount);
    Data->SetArrayField(TEXT("items"), Items);
    return Data;
}
}
