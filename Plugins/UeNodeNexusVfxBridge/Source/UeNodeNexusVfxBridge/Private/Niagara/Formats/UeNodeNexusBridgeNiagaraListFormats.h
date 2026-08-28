#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UNiagaraSystem;

namespace UeNodeNexusBridge::NiagaraListFormats
{
// "NiagaraSpriteRendererProperties" -> "Sprite"; shared by renderer and
// material list text formatters.
FString ShortRendererClass(const FString& ClassName);
bool IsSupportedFormat(const FString& Format);
TSharedPtr<FJsonObject> MakeInvalidFormatResponse(const FString& Operation, const FString& RequestId, const FString& Format);
TSharedPtr<FJsonObject> BuildRenderersListData(UNiagaraSystem* System, const FString& Format);
TSharedPtr<FJsonObject> BuildMaterialsListData(UNiagaraSystem* System, const FString& Format);
}
