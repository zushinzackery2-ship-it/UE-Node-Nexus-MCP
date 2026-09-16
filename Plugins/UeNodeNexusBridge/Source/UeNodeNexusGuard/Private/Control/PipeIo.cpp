#include "PipeIo.h"
#include "State.h"
#include "../Identity/Identity.h"

#include "Serialization/JsonSerializer.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <sddl.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
static bool Finish(HANDLE Pipe, OVERLAPPED& Overlapped, HANDLE Stop, DWORD& Count, DWORD Timeout)
{
    HANDLE Events[2];
    Events[0] = Overlapped.hEvent;
    Events[1] = Stop;
    if (WaitForMultipleObjects(2, Events, false, Timeout) != WAIT_OBJECT_0)
    {
        CancelIoEx(Pipe, &Overlapped);
        GetOverlappedResult(Pipe, &Overlapped, &Count, true);
        return false;
    }
    return GetOverlappedResult(Pipe, &Overlapped, &Count, false) != 0;
}

bool Connect(void* Pipe, void* Event, void* Stop)
{
    OVERLAPPED Overlapped;
    FMemory::Memzero(Overlapped);
    Overlapped.hEvent = Event;
    ResetEvent(Event);
    if (ConnectNamedPipe(Pipe, &Overlapped))
    {
        return true;
    }
    const DWORD Error = GetLastError();
    DWORD Count = 0;
    // Keep an idle accept alive until connection or shutdown. Periodic cancellation
    // races with CreateFile succeeding and can disconnect a newly connected client.
    return Error == ERROR_PIPE_CONNECTED || (Error == ERROR_IO_PENDING && Finish(Pipe, Overlapped, Stop, Count, INFINITE));
}

bool Transfer(void* Pipe, void* Event, void* Stop, uint8* Buffer, uint32 Size, bool bWrite)
{
    uint32 Offset = 0;
    const ULONGLONG Deadline = GetTickCount64() + 2500;
    while (Offset < Size && GetTickCount64() < Deadline)
    {
        OVERLAPPED Overlapped;
        FMemory::Memzero(Overlapped);
        Overlapped.hEvent = Event;
        ResetEvent(Event);
        DWORD Count = 0;
        const bool bImmediate = bWrite
            ? WriteFile(Pipe, Buffer + Offset, Size - Offset, &Count, &Overlapped) != 0
            : ReadFile(Pipe, Buffer + Offset, Size - Offset, &Count, &Overlapped) != 0;
        const DWORD Error = bImmediate ? ERROR_SUCCESS : GetLastError();
        const ULONGLONG Now = GetTickCount64();
        const DWORD Remaining = Now < Deadline ? static_cast<DWORD>(Deadline - Now) : 0;
        if (!bImmediate && (Error != ERROR_IO_PENDING
            || !Finish(Pipe, Overlapped, Stop, Count, Remaining)))
        {
            return false;
        }
        if (Count == 0)
        {
            return false;
        }
        Offset += Count;
    }
    return Offset == Size;
}

void* CreateSecurity()
{
    HANDLE Token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        return nullptr;
    }
    DWORD Size = 0;
    GetTokenInformation(Token, TokenUser, nullptr, 0, &Size);
    TArray<uint8> Buffer;
    Buffer.SetNumZeroed(Size);
    LPWSTR Sid = nullptr;
    const bool bValid = GetTokenInformation(Token, TokenUser, Buffer.GetData(), Size, &Size)
        && ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(Buffer.GetData())->User.Sid, &Sid);
    CloseHandle(Token);
    if (!bValid)
    {
        return nullptr;
    }
    const FString Descriptor = FString(TEXT("D:P(A;;GA;;;SY)(A;;GA;;;")) + Sid + TEXT(")S:(ML;;NW;;;ME)");
    LocalFree(Sid);
    PSECURITY_DESCRIPTOR Security = nullptr;
    ConvertStringSecurityDescriptorToSecurityDescriptorW(*Descriptor, SDDL_REVISION_1, &Security, nullptr);
    return Security;
}

void ServeRequest(void* Pipe, void* Event, void* Stop)
{
    uint32 Size = 0;
    if (!Transfer(Pipe, Event, Stop, reinterpret_cast<uint8*>(&Size), 4, false) || Size == 0 || Size > 1024 * 1024)
    {
        return;
    }
    TArray<uint8> Body;
    Body.SetNumUninitialized(Size + 1);
    if (!Transfer(Pipe, Event, Stop, Body.GetData(), Size, false))
    {
        return;
    }
    Body[Size] = 0;
    TSharedPtr<FJsonObject> Request;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(UTF8_TO_TCHAR(Body.GetData()))), Request);
    ULONG Peer = 0;
    if (!GetNamedPipeClientProcessId(Pipe, &Peer))
    {
        return;
    }
    const FString Response = Encode(Dispatch(Request, Peer));
    FTCHARToUTF8 Utf8(*Response);
    Size = Utf8.Length();
    if (!Transfer(Pipe, Event, Stop, reinterpret_cast<uint8*>(&Size), 4, true)
        || !Transfer(Pipe, Event, Stop, reinterpret_cast<uint8*>(const_cast<ANSICHAR*>(Utf8.Get())), Size, true))
    {
        return;
    }
    uint8 Eof;
    Transfer(Pipe, Event, Stop, &Eof, 1, false);
}
}
