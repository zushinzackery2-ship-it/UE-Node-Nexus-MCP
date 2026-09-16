#include "Pipe.h"
#include "PipeIo.h"
#include "State.h"

#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
class FWatchdogTask final : public FRunnable
{
public:
    explicit FWatchdogTask(HANDLE InStop) : Stop(InStop)
    {
    }

    virtual uint32 Run() override
    {
        while (WaitForSingleObject(Stop, 15000) == WAIT_TIMEOUT)
        {
            Watchdog();
        }
        return 0;
    }

private:
    HANDLE Stop;
};

FPipe::FPipe()
{
    StopEvent = CreateEventW(nullptr, true, false, nullptr);
    Security = CreateSecurity();
    if (!Security || !StopEvent)
    {
        UE_LOG(LogTemp, Fatal, TEXT("Nexus lifecycle pipe initialization failed"));
    }
    for (int32 Index = 0; Index < 4; ++Index)
    {
        Threads.Add(FRunnableThread::Create(this, *FString::Printf(TEXT("NexusLifecycle%d"), Index), 0, TPri_BelowNormal));
    }
    WatchdogTask = MakeUnique<FWatchdogTask>(StopEvent);
    Threads.Add(FRunnableThread::Create(WatchdogTask.Get(), TEXT("NexusLifecycleWatchdog"), 0, TPri_BelowNormal));
}

FPipe::~FPipe()
{
    Stop();
    for (FRunnableThread* Thread : Threads)
    {
        if (Thread)
        {
            Thread->WaitForCompletion();
            delete Thread;
        }
    }
    CloseHandle(StopEvent);
    LocalFree(Security);
}

void FPipe::Stop()
{
    SetEvent(StopEvent);
}

uint32 FPipe::Run()
{
    const FString Name = FString::Printf(TEXT("\\\\.\\pipe\\UeNodeNexusLifecycle.%u"), GetCurrentProcessId());
    HANDLE Event = CreateEventW(nullptr, true, false, nullptr);
    SECURITY_ATTRIBUTES Attributes;
    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = Security;
    Attributes.bInheritHandle = false;
    while (WaitForSingleObject(StopEvent, 0) != WAIT_OBJECT_0)
    {
        HANDLE Pipe = CreateNamedPipeW(*Name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            4, 65536, 65536, 1000, &Attributes);
        if (Pipe == INVALID_HANDLE_VALUE)
        {
            WaitForSingleObject(StopEvent, 1000);
            continue;
        }
        if (Connect(Pipe, Event, StopEvent))
        {
            ServeRequest(Pipe, Event, StopEvent);
        }
        DisconnectNamedPipe(Pipe);
        CloseHandle(Pipe);
    }
    CloseHandle(Event);
    return 0;
}
}
