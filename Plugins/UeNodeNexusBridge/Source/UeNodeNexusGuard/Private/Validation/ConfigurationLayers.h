#pragma once

#include "Misc/ConfigCacheIni.h"

namespace NexusLifecycle::Validation
{
enum class EConfigurationPhase
{
    Startup,
    Runtime
};

FConfigFile EffectiveConfigurationFile(const FConfigFile& Runtime);
void ApplyConfigurationFile(FConfigFile& Runtime, const FConfigFile& Snapshot, EConfigurationPhase Phase);
}
