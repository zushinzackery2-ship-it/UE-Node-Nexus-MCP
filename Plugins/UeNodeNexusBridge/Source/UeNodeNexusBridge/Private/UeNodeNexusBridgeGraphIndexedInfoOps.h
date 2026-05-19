#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
FString EscapeIndexedToken(const FString& Input);
int32 DictIndex(TMap<FString, int32>& Dict, TArray<FString>& Items, const FString& Value);
FString JoinDictionaryLine(const FString& Prefix, const TArray<FString>& Items);
bool WantsRealIds(const TSharedPtr<FJsonObject>& Payload);
int32 ReadIndexedMaxNodes(const TSharedPtr<FJsonObject>& Payload);
}
