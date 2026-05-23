#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UNiagaraSystem;

namespace UeNodeNexusBridge::NiagaraListFormats
{
bool IsSupportedFormat(const FString& Format);
TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format);
TSharedPtr<FJsonObject> BuildRenderersListData(UNiagaraSystem* System, const FString& Format);
TSharedPtr<FJsonObject> BuildMaterialsListData(UNiagaraSystem* System, const FString& Format);
}
