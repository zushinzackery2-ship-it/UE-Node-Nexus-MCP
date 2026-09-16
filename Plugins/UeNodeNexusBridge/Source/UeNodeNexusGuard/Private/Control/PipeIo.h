#pragma once

#include "CoreMinimal.h"

namespace NexusLifecycle
{
bool Transfer(void* Pipe, void* Event, void* Stop, uint8* Buffer, uint32 Size, bool bWrite);
bool Connect(void* Pipe, void* Event, void* Stop);
void* CreateSecurity();
void ServeRequest(void* Pipe, void* Event, void* Stop);
}
