#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge::NiagaraIndexedFormat
{
FString EscapeToken(const FString& Input);
int32 DictIndex(TMap<FString, int32>& Dict, TArray<FString>& Items, const FString& Value);
FString JoinDictionaryLine(const FString& Prefix, const TArray<FString>& Items);
void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text);
}
