#pragma once

#include "Misc/ConfigCacheIni.h"

namespace NexusLifecycle::Tests
{
// Every key of the file as ini text; with a branch attached, WriteToString writes only the differences from its combined layers.
inline FString ConfigText(FConfigFile File)
{
    File.Branch = nullptr;
    FString Text;
    File.WriteToString(Text);
    return Text;
}
}
