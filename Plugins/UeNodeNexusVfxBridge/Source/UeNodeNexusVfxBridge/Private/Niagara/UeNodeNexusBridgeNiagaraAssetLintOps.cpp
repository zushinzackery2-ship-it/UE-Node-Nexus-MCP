#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Lint/UeNodeNexusBridgeNiagaraAssetLintRules.h"
#include "Lint/UeNodeNexusBridgeNiagaraAssetLintShared.h"

#include "Dom/JsonValue.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge
{

TSharedPtr<FJsonObject> HandleNiagaraAssetLint(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Format = TEXT("indexed");
    if (Payload.IsValid())
    {
        Payload->TryGetStringField(TEXT("format"), Format);
    }
    if (!NiagaraAssetLint::IsSupportedLintFormat(Format))
    {
        return NiagaraAssetLint::MakeInvalidLintFormatResponse(Operation, RequestId, Format);
    }

    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    TArray<TSharedPtr<FJsonValue>> Issues;
    NiagaraAssetLint::CollectSystemIssues(System, Issues);

    const NiagaraAssetLint::FLintSeverityCounts SeverityCounts = NiagaraAssetLint::CountSeverities(Issues);
    const int32 BlockingIssueCount = SeverityCounts.ErrorCount + SeverityCounts.WarningCount + SeverityCounts.OtherCount;

    TSharedPtr<FJsonObject> Data;
    if (Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase))
    {
        Data = NiagaraAssetLint::BuildLintIndexedData(System, Issues, SeverityCounts, BlockingIssueCount);
    }
    else if (Format.Equals(TEXT("tiny"), ESearchCase::IgnoreCase))
    {
        Data = NiagaraAssetLint::BuildLintTinyData(System, Issues, SeverityCounts, BlockingIssueCount);
    }
    else
    {
        Data = MakeNiagaraAssetSummaryData(System);
        Data->SetStringField(TEXT("format"), TEXT("niagara_asset_lint_full"));
        Data->SetArrayField(TEXT("issues"), Issues);
        NiagaraAssetLint::AddLintCounts(Data, SeverityCounts, Issues.Num(), BlockingIssueCount);
    }

    Data->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    Data->SetNumberField(TEXT("enabled_emitter_count"), CountEnabledNiagaraEmitters(System));
    Data->SetNumberField(TEXT("renderer_count"), CountNiagaraRenderers(System));
    Data->SetNumberField(TEXT("enabled_renderer_count"), CountEnabledNiagaraRenderers(System));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
