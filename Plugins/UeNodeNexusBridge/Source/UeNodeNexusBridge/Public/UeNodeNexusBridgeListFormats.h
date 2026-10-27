#pragma once

#include "CoreMinimal.h"

#include <initializer_list>

class FJsonObject;

namespace UeNodeNexusBridge
{
// Compact listings: the column names of every row array in Data.
UENODENEXUSBRIDGE_API void SetColumns(const TSharedPtr<FJsonObject>& Data, std::initializer_list<const TCHAR*> Columns);

// Indexed listings: one text payload whose rows reference per-response dictionaries by index.
UENODENEXUSBRIDGE_API FString EscapeIndexedToken(const FString& Input);
UENODENEXUSBRIDGE_API int32 DictIndex(TMap<FString, int32>& Dict, TArray<FString>& Items, const FString& Value);
UENODENEXUSBRIDGE_API FString JoinDictionaryLine(const FString& Prefix, const TArray<FString>& Items);
}
