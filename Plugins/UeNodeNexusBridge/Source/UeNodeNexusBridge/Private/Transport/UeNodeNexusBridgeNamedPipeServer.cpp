#include "UeNodeNexusBridgeNamedPipeServer.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNamedPipeIo.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUeNodeNexusBridge, Log, All);

namespace
{
// One pipe instance per worker; PIPE_UNLIMITED_INSTANCES lets concurrent MCP
// sessions (multiple Agents talking to the same editor) each grab a free slot.
static constexpr int32 WorkerCount = 4;
// Safety cap so a malformed length prefix cannot make us allocate wildly.
static constexpr uint32 MaxFrameBytes = 64u * 1024u * 1024u;
static constexpr uint32 PipeBufferHint = 64u * 1024u;

static FString MakePipeName()
{
    return FString::Printf(TEXT("\\\\.\\pipe\\UeNodeNexusBridge.%u"), FPlatformProcess::GetCurrentProcessId());
}
}
class FUeNodeNexusBridgeNamedPipeServer::FWorker : public FRunnable
{
public:
    FWorker(const FString& InPipeName, void* InStopEvent)
        : PipeName(InPipeName)
        , StopEvent(InStopEvent)
    {
    }

    virtual uint32 Run() override;
    virtual void Stop() override { bStopping = true; }

    void Launch(int32 Index)
    {
        Thread = FRunnableThread::Create(this, *FString::Printf(TEXT("UeNexusPipe_%d"), Index), 0, TPri_Normal);
    }

    void JoinAndDestroy()
    {
        bStopping = true;
        if (Thread != nullptr)
        {
            Thread->WaitForCompletion();
            delete Thread;
            Thread = nullptr;
        }
    }

private:
    FString PipeName;
    void* StopEvent = nullptr;   // shared manual-reset event (not owned)
    FRunnableThread* Thread = nullptr;
    FThreadSafeBool bStopping = false;

#if PLATFORM_WINDOWS
    bool ServeConnection(HANDLE Pipe, HANDLE IoEvent);
#endif
};

#if PLATFORM_WINDOWS
uint32 FUeNodeNexusBridgeNamedPipeServer::FWorker::Run()
{
    // Manual-reset event for overlapped I/O completion; reset before each op.
    HANDLE IoEvent = CreateEventW(nullptr, true, false, nullptr);
    if (IoEvent == nullptr)
    {
        UE_LOG(LogUeNodeNexusBridge, Error, TEXT("Named pipe worker failed to create IO event (err=%u)"), GetLastError());
        return 1;
    }

    const HANDLE StopHandle = static_cast<HANDLE>(StopEvent);

    while (!bStopping)
    {
        HANDLE Pipe = CreateNamedPipeW(
            *PipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            PipeBufferHint,
            PipeBufferHint,
            0,
            nullptr);

        if (Pipe == INVALID_HANDLE_VALUE)
        {
            UE_LOG(LogUeNodeNexusBridge, Error, TEXT("CreateNamedPipe failed (err=%u) for %s"), GetLastError(), *PipeName);
            if (WaitForSingleObject(StopHandle, 500) == WAIT_OBJECT_0)
            {
                break;
            }
            continue;
        }

        // Wait (overlapped) for a client to connect, or bail on stop.
        OVERLAPPED Ov;
        FMemory::Memzero(Ov);
        ResetEvent(IoEvent);
        Ov.hEvent = IoEvent;

        bool bConnected = false;
        if (ConnectNamedPipe(Pipe, &Ov))
        {
            bConnected = true;   // connected synchronously (rare)
        }
        else
        {
            const DWORD ConnectErr = GetLastError();
            if (ConnectErr == ERROR_IO_PENDING)
            {
                HANDLE WaitHandles[2] = { IoEvent, StopHandle };
                const DWORD Wait = WaitForMultipleObjects(2, WaitHandles, false, INFINITE);
                if (Wait == WAIT_OBJECT_0)
                {
                    DWORD Transferred = 0;
                    bConnected = GetOverlappedResult(Pipe, &Ov, &Transferred, false) != 0;
                }
                else
                {
                    CancelIoEx(Pipe, &Ov);
                    CloseHandle(Pipe);
                    break;   // stop signaled
                }
            }
            else if (ConnectErr == ERROR_PIPE_CONNECTED)
            {
                bConnected = true;   // client connected before ConnectNamedPipe
            }
        }

        if (bConnected && !bStopping)
        {
            ServeConnection(Pipe, IoEvent);
        }

        FlushFileBuffers(Pipe);
        DisconnectNamedPipe(Pipe);
        CloseHandle(Pipe);
    }

    CloseHandle(IoEvent);
    return 0;
}

bool FUeNodeNexusBridgeNamedPipeServer::FWorker::ServeConnection(HANDLE Pipe, HANDLE IoEvent)
{
    // Frame = 4-byte little-endian length + UTF-8 JSON body.
    uint8 LengthBytes[4];
    if (!UeNodeNexusBridge::ReadNamedPipeExact(
        Pipe,
        IoEvent,
        StopEvent,
        bStopping,
        LengthBytes,
        4))
    {
        return false;
    }
    const uint32 BodyLength =
        static_cast<uint32>(LengthBytes[0]) |
        (static_cast<uint32>(LengthBytes[1]) << 8) |
        (static_cast<uint32>(LengthBytes[2]) << 16) |
        (static_cast<uint32>(LengthBytes[3]) << 24);

    if (BodyLength == 0 || BodyLength > MaxFrameBytes)
    {
        UE_LOG(LogUeNodeNexusBridge, Warning, TEXT("Named pipe rejected frame length %u"), BodyLength);
        return false;
    }

    TArray<uint8> BodyBytes;
    BodyBytes.SetNumUninitialized(static_cast<int32>(BodyLength));
    if (!UeNodeNexusBridge::ReadNamedPipeExact(
        Pipe,
        IoEvent,
        StopEvent,
        bStopping,
        BodyBytes.GetData(),
        BodyLength))
    {
        return false;
    }

    const FString Body = UeNodeNexusBridge::BodyToString(BodyBytes);

    FString ResponseString;
    if (!UeNodeNexusBridge::DispatchNamedPipeRequest(Body, bStopping, ResponseString))
    {
        return false;   // stopping mid-flight
    }

    const FTCHARToUTF8 Utf8(*ResponseString);
    const uint32 RespLength = static_cast<uint32>(Utf8.Length());
    uint8 RespLengthBytes[4];
    RespLengthBytes[0] = static_cast<uint8>(RespLength & 0xFF);
    RespLengthBytes[1] = static_cast<uint8>((RespLength >> 8) & 0xFF);
    RespLengthBytes[2] = static_cast<uint8>((RespLength >> 16) & 0xFF);
    RespLengthBytes[3] = static_cast<uint8>((RespLength >> 24) & 0xFF);

    if (!UeNodeNexusBridge::WriteNamedPipeAll(
        Pipe,
        IoEvent,
        StopEvent,
        bStopping,
        RespLengthBytes,
        4))
    {
        return false;
    }
    if (RespLength > 0
        && !UeNodeNexusBridge::WriteNamedPipeAll(
            Pipe,
            IoEvent,
            StopEvent,
            bStopping,
            reinterpret_cast<const uint8*>(Utf8.Get()),
            RespLength))
    {
        return false;
    }
    return true;
}
#else  // !PLATFORM_WINDOWS
uint32 FUeNodeNexusBridgeNamedPipeServer::FWorker::Run()
{
    return 0;
}
#endif  // PLATFORM_WINDOWS
FUeNodeNexusBridgeNamedPipeServer::FUeNodeNexusBridgeNamedPipeServer() = default;

FUeNodeNexusBridgeNamedPipeServer::~FUeNodeNexusBridgeNamedPipeServer()
{
    Stop();
}

void FUeNodeNexusBridgeNamedPipeServer::Start()
{
#if PLATFORM_WINDOWS
    if (Workers.Num() > 0)
    {
        return;   // already running
    }

    StopEvent = CreateEventW(nullptr, true, false, nullptr);
    if (StopEvent == nullptr)
    {
        UE_LOG(LogUeNodeNexusBridge, Error, TEXT("Failed to create named pipe stop event (err=%u)"), GetLastError());
        return;
    }

    const FString PipeName = MakePipeName();
    for (int32 Index = 0; Index < WorkerCount; ++Index)
    {
        TUniquePtr<FWorker> Worker = MakeUnique<FWorker>(PipeName, StopEvent);
        Worker->Launch(Index);
        Workers.Add(MoveTemp(Worker));
    }

    UE_LOG(LogUeNodeNexusBridge, Display, TEXT("UE Node Nexus Bridge listening on named pipe %s (%d workers)"), *PipeName, WorkerCount);
#else
    UE_LOG(LogUeNodeNexusBridge, Warning, TEXT("UE Node Nexus Bridge named pipe transport requires Windows; bridge disabled"));
#endif
}

void FUeNodeNexusBridgeNamedPipeServer::Stop()
{
#if PLATFORM_WINDOWS
    if (StopEvent != nullptr)
    {
        SetEvent(static_cast<HANDLE>(StopEvent));   // wake every blocking wait
    }

    for (const TUniquePtr<FWorker>& Worker : Workers)
    {
        Worker->JoinAndDestroy();
    }
    Workers.Empty();

    if (StopEvent != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(StopEvent));
        StopEvent = nullptr;
    }
#endif
}
