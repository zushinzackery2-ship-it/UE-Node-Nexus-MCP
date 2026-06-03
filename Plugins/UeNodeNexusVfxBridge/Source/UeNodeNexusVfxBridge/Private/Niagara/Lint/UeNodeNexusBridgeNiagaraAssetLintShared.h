#pragma once

#include "CoreMinimal.h"

class UNiagaraSystem;
class FJsonObject;
class FJsonValue;

namespace UeNodeNexusBridge::NiagaraAssetLint
{
struct FLintSeverityCounts
{
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    int32 InfoCount = 0;
    int32 OtherCount = 0;
};

TSharedPtr<FJsonObject> MakeLintIssue(
    const FString& Severity,
    const FString& Code,
    const FString& Message,
    int32 EmitterIndex = INDEX_NONE,
    int32 RendererIndex = INDEX_NONE,
    const FString& ModuleUsage = FString(),
    int32 ModuleIndex = INDEX_NONE);

void AddIssue(TArray<TSharedPtr<FJsonValue>>& Issues, const TSharedPtr<FJsonObject>& Issue);
FLintSeverityCounts CountSeverities(const TArray<TSharedPtr<FJsonValue>>& Issues);
bool IsSupportedLintFormat(const FString& Format);
TSharedPtr<FJsonObject> MakeInvalidLintFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format);
void AddLintCounts(TSharedPtr<FJsonObject> Data, const FLintSeverityCounts& SeverityCounts, int32 IssueCount, int32 BlockingIssueCount);
TSharedPtr<FJsonObject> BuildLintIndexedData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Issues, const FLintSeverityCounts& SeverityCounts, int32 BlockingIssueCount);
TSharedPtr<FJsonObject> BuildLintTinyData(UNiagaraSystem* System, const TArray<TSharedPtr<FJsonValue>>& Issues, const FLintSeverityCounts& SeverityCounts, int32 BlockingIssueCount);
}
