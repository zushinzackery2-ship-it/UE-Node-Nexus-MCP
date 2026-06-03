#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeNiagaraHelpers.h"
#include "Formats/UeNodeNexusBridgeNiagaraSummaryFormats.h"

#include "Dom/JsonValue.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleNiagaraSystemSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!NiagaraSummaryFormats::IsSupportedFormat(Format))
    {
        return NiagaraSummaryFormats::MakeInvalidFormatResponse(Operation, RequestId, Format);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), NiagaraSummaryFormats::BuildSystemSummaryData(System, Format));
    TArray<TSharedPtr<FJsonValue>> Warnings;
    AppendNiagaraEmptySystemWarning(System, Warnings);
    if (Warnings.Num() > 0)
    {
        Response->SetArrayField(TEXT("warnings"), Warnings);
    }
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

    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    if (!NiagaraSummaryFormats::IsSupportedFormat(Format))
    {
        return NiagaraSummaryFormats::MakeInvalidFormatResponse(Operation, RequestId, Format);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), NiagaraSummaryFormats::BuildEmittersListData(System, Format));
    TArray<TSharedPtr<FJsonValue>> Warnings;
    AppendNiagaraEmptySystemWarning(System, Warnings);
    if (Warnings.Num() > 0)
    {
        Response->SetArrayField(TEXT("warnings"), Warnings);
    }
    return Response;
}
}
