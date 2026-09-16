#include "Identity.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
TSharedPtr<FJsonObject> ProcessIdentity(uint32 Pid)
{
    HANDLE Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, false, Pid);
    if (!Process)
    {
        return nullptr;
    }
    FILETIME Created, Exited, Kernel, User;
    FMemory::Memzero(Created);
    WCHAR Buffer[32768];
    DWORD Size = UE_ARRAY_COUNT(Buffer);
    const bool bValid = WaitForSingleObject(Process, 0) == WAIT_TIMEOUT
        && GetProcessTimes(Process, &Created, &Exited, &Kernel, &User)
        && QueryFullProcessImageNameW(Process, 0, Buffer, &Size);
    CloseHandle(Process);
    if (!bValid)
    {
        return nullptr;
    }
    const uint64 Time = (static_cast<uint64>(Created.dwHighDateTime) << 32) | Created.dwLowDateTime;
    FString Executable(Buffer);
    Executable.ReplaceInline(TEXT("\\"), TEXT("/"));
    auto Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("pid"), Pid);
    Result->SetStringField(TEXT("process_created"), LexToString(Time));
    Result->SetStringField(TEXT("executable"), Executable.ToLower());
    return Result;
}

bool SameProcess(const TSharedPtr<FJsonObject>& Identity)
{
    double Pid = 0;
    FString Created, Executable;
    if (!Identity.IsValid() || !Identity->TryGetNumberField(TEXT("pid"), Pid)
        || !Identity->TryGetStringField(TEXT("process_created"), Created)
        || !Identity->TryGetStringField(TEXT("executable"), Executable))
    {
        return false;
    }
    const auto Actual = ProcessIdentity(static_cast<uint32>(Pid));
    return Actual.IsValid() && Actual->GetStringField(TEXT("process_created")) == Created
        && Actual->GetStringField(TEXT("executable")) == Executable;
}
}
