#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "MaterialTypes.h"

namespace UeNodeNexusBridge::Transcode
{
bool InstanceParameterInfo(const TSharedPtr<FJsonObject>& Op, FMaterialParameterInfo& Out, FString& Error);
bool InstanceFloat(const FString& Text, float& Out);
bool InstanceBool(const FString& Text, bool& Out);
bool InstanceChannels(const FString& Text, TArray<FString>& Out);
}
