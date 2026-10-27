#pragma once

#include "CoreMinimal.h"

namespace UeNodeNexusBridge
{
UENODENEXUSBRIDGE_API bool ParseAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FText& OutReason);
// A long package name gains its asset object name ("/Game/A" -> "/Game/A.A"); object paths and invalid input are returned unchanged.
UENODENEXUSBRIDGE_API FString NormalizeAssetObjectPath(const FString& AssetPath);
}
