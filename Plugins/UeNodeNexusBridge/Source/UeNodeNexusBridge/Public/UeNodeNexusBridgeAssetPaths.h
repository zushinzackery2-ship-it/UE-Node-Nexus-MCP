#pragma once

#include "CoreMinimal.h"

namespace UeNodeNexusBridge
{
UENODENEXUSBRIDGE_API bool ParseAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FText& OutReason);
}
