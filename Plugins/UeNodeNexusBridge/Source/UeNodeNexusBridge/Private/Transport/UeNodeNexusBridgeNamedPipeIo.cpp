#include "UeNodeNexusBridgeNamedPipeIo.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "HAL/PlatformProcess.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UeNodeNexusBridge
{
bool ReadNamedPipeExact(
    void* PipeHandle,
    void* IoEventHandle,
    void* StopEventHandle,
    FThreadSafeBool& Stopping,
    uint8* Buffer,
    uint32 NumBytes)
{
#if PLATFORM_WINDOWS
    const HANDLE Pipe = static_cast<HANDLE>(PipeHandle);
    const HANDLE IoEvent = static_cast<HANDLE>(IoEventHandle);
    const HANDLE StopEvent = static_cast<HANDLE>(StopEventHandle);
    uint32 Total = 0;
    while (Total < NumBytes)
    {
        if (Stopping)
        {
            return false;
        }

        OVERLAPPED Overlapped;
        FMemory::Memzero(Overlapped);
        ResetEvent(IoEvent);
        Overlapped.hEvent = IoEvent;

        DWORD Read = 0;
        if (!ReadFile(Pipe, Buffer + Total, NumBytes - Total, &Read, &Overlapped))
        {
            const DWORD Error = GetLastError();
            if (Error != ERROR_IO_PENDING)
            {
                return false;
            }

            HANDLE WaitHandles[2] = {IoEvent, StopEvent};
            if (WaitForMultipleObjects(2, WaitHandles, false, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIoEx(Pipe, &Overlapped);
                GetOverlappedResult(Pipe, &Overlapped, &Read, true);
                return false;
            }
            if (!GetOverlappedResult(Pipe, &Overlapped, &Read, false))
            {
                return false;
            }
        }

        if (Read == 0)
        {
            return false;
        }
        Total += Read;
    }
    return true;
#else
    return false;
#endif
}

bool WriteNamedPipeAll(
    void* PipeHandle,
    void* IoEventHandle,
    void* StopEventHandle,
    FThreadSafeBool& Stopping,
    const uint8* Buffer,
    uint32 NumBytes)
{
#if PLATFORM_WINDOWS
    const HANDLE Pipe = static_cast<HANDLE>(PipeHandle);
    const HANDLE IoEvent = static_cast<HANDLE>(IoEventHandle);
    const HANDLE StopEvent = static_cast<HANDLE>(StopEventHandle);
    uint32 Total = 0;
    while (Total < NumBytes)
    {
        if (Stopping)
        {
            return false;
        }

        OVERLAPPED Overlapped;
        FMemory::Memzero(Overlapped);
        ResetEvent(IoEvent);
        Overlapped.hEvent = IoEvent;

        DWORD Written = 0;
        if (!WriteFile(Pipe, Buffer + Total, NumBytes - Total, &Written, &Overlapped))
        {
            const DWORD Error = GetLastError();
            if (Error != ERROR_IO_PENDING)
            {
                return false;
            }

            HANDLE WaitHandles[2] = {IoEvent, StopEvent};
            if (WaitForMultipleObjects(2, WaitHandles, false, INFINITE) != WAIT_OBJECT_0)
            {
                CancelIoEx(Pipe, &Overlapped);
                GetOverlappedResult(Pipe, &Overlapped, &Written, true);
                return false;
            }
            if (!GetOverlappedResult(Pipe, &Overlapped, &Written, false))
            {
                return false;
            }
        }
        Total += Written;
    }
    return true;
#else
    return false;
#endif
}

bool DispatchNamedPipeRequest(
    const FString& Body,
    void* PipeHandle,
    FThreadSafeBool& Stopping,
    FString& OutResponse)
{
    TSharedPtr<FJsonObject> Request;
    uint32 Peer = 0;
#if PLATFORM_WINDOWS
    ULONG NativePeer = 0;
    if (!GetNamedPipeClientProcessId(PipeHandle, &NativePeer))
    {
        return false;
    }
    Peer = NativePeer;
#endif
    if (!PrepareBridgeRequest(Body, Request, OutResponse, Peer))
    {
        return true;
    }
    TSharedRef<TPromise<FString>, ESPMode::ThreadSafe> Promise =
        MakeShared<TPromise<FString>, ESPMode::ThreadSafe>();
    TFuture<FString> Future = Promise->GetFuture();

    AsyncTask(ENamedThreads::GameThread, [Request, Promise]()
    {
        Promise->SetValue(DispatchParsedRequest(Request));
    });

    // Stay responsive to ShutdownModule on the game thread instead of waiting
    // unconditionally for a task that could no longer execute.
    while (!Stopping)
    {
        if (Future.IsReady())
        {
            OutResponse = Future.Get();
            return true;
        }
        FPlatformProcess::Sleep(0.01f);
    }
    return false;
}
}
