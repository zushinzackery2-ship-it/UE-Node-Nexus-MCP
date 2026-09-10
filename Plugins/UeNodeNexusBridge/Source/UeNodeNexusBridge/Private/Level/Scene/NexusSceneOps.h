#pragma once

#include "NexusSceneJson.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleSceneExport(const FString&, const FString&, const TSharedPtr<FJsonObject>&);
TSharedPtr<FJsonObject> HandleSceneStatus(const FString&, const FString&, const TSharedPtr<FJsonObject>&);
TSharedPtr<FJsonObject> HandleSceneApply(const FString&, const FString&, const TSharedPtr<FJsonObject>&);
TSharedPtr<FJsonObject> HandleComponentInstancesGet(const FString&, const FString&, const TSharedPtr<FJsonObject>&);
TSharedPtr<FJsonObject> HandleComponentInstancesPatch(const FString&, const FString&, const TSharedPtr<FJsonObject>&);
}
