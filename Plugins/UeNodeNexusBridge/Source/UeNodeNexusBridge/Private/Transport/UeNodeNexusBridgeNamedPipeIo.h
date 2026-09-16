#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"

namespace UeNodeNexusBridge
{
bool ReadNamedPipeExact(
    void* PipeHandle,
    void* IoEventHandle,
    void* StopEventHandle,
    FThreadSafeBool& Stopping,
    uint8* Buffer,
    uint32 NumBytes);
bool WriteNamedPipeAll(
    void* PipeHandle,
    void* IoEventHandle,
    void* StopEventHandle,
    FThreadSafeBool& Stopping,
    const uint8* Buffer,
    uint32 NumBytes);
bool DispatchNamedPipeRequest(
    const FString& Body,
    void* PipeHandle,
    FThreadSafeBool& Stopping,
    FString& OutResponse);
}
