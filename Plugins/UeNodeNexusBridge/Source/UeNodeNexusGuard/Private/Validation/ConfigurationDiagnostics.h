#pragma once

#include "Misc/ConfigCacheIni.h"

namespace NexusLifecycle::Validation
{
void LogConfigurationLayers(const FString& Category, const FConfigFile& Runtime, const FConfigFile& Effective);
}
