#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> LoadedAssetToJson(UObject* Asset, const FString& NormalizedAssetPath);
}
