#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UNiagaraSystem;

namespace UeNodeNexusBridge::NiagaraSummaryFormats
{
bool IsSupportedFormat(const FString& Format);
TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format);
TSharedPtr<FJsonObject> BuildSystemSummaryData(UNiagaraSystem* System, const FString& Format);
TSharedPtr<FJsonObject> BuildEmittersListData(UNiagaraSystem* System, const FString& Format);
}
