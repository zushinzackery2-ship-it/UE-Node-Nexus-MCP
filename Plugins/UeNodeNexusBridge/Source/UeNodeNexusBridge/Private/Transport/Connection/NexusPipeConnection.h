#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"

namespace UeNodeNexusBridge
{
bool ServeNamedPipeConnection(void* Pipe, void* IoEvent, void* StopEventHandle, FThreadSafeBool& Stopping);
}
