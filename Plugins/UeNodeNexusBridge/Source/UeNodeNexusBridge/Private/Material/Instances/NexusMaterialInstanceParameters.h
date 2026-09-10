#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UMaterialInstanceConstant;

namespace UeNodeNexusBridge::MaterialInstances
{
bool ApplyScalar(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param);
bool ApplyVector(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param);
bool ApplyTexture(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param);
bool ApplyStaticSwitch(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param);
}
