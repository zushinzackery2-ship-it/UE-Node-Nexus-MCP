#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeExit.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusTests
{
inline bool AdvanceNativeFileStamp(const FString& File, uint64 Ticks)
{
    HANDLE Handle = CreateFileW(*File, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    ON_SCOPE_EXIT
    {
        CloseHandle(Handle);
    };
    FILETIME Stamp;
    if (!GetFileTime(Handle, nullptr, nullptr, &Stamp))
    {
        return false;
    }
    ULARGE_INTEGER Value;
    Value.HighPart = Stamp.dwHighDateTime;
    Value.LowPart = Stamp.dwLowDateTime;
    Value.QuadPart += Ticks;
    Stamp.dwHighDateTime = Value.HighPart;
    Stamp.dwLowDateTime = Value.LowPart;
    return SetFileTime(Handle, nullptr, nullptr, &Stamp) != 0;
}
}
#endif
