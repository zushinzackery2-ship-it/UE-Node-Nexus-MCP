#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "UeNodeNexusBridgeNamedPipeIo.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusPipeResponseCompletion,
    "Nexus.Issues10.Transport.ResponseCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusPipeResponseCompletion::RunTest(const FString& Parameters)
{
    for (bool bConsume : { false, true })
    {
        const FString Name = TEXT("\\\\.\\pipe\\NexusCompletionTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        HANDLE Pipe = CreateNamedPipeW(*Name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
        HANDLE Client = CreateFileW(*Name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        HANDLE Io = CreateEventW(nullptr, true, false, nullptr);
        HANDLE Stop = CreateEventW(nullptr, true, false, nullptr);
        HANDLE Written = CreateEventW(nullptr, true, false, nullptr);
        if (!TestTrue(TEXT("isolated pipe handles exist"), Pipe != INVALID_HANDLE_VALUE
            && Client != INVALID_HANDLE_VALUE && Io && Stop && Written))
        {
            for (HANDLE Handle : { Client, Pipe, Io, Stop, Written })
            {
                if (Handle && Handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(Handle);
                }
            }
            return false;
        }
        OVERLAPPED Connection = {};
        Connection.hEvent = Io;
        TestTrue(TEXT("isolated client connected"), ConnectNamedPipe(Pipe, &Connection) || GetLastError() == ERROR_PIPE_CONNECTED);
        FThreadSafeBool Stopping = false;
        TArray<uint8> Response;
        Response.Init(0xA7, 4096);
        auto Worker = Async(EAsyncExecution::Thread, [&]()
        {
            const bool bWritten = UeNodeNexusBridge::WriteNamedPipeAll(Pipe, Io, Stop, Stopping,
                Response.GetData(), Response.Num());
            SetEvent(Written);
            if (bWritten)
            {
                UeNodeNexusBridge::AwaitNamedPipePeerClose(Pipe, Io, Stop, Stopping);
            }
            return bWritten;
        });
        const bool bWritten = WaitForSingleObject(Written, 3000) == WAIT_OBJECT_0;
        TestTrue(TEXT("response fits and reaches the pipe buffer"), bWritten);
        TestFalse(TEXT("buffered output remains available until client finishes"), Worker.WaitFor(FTimespan::FromMilliseconds(50)));
        if (bConsume && bWritten)
        {
            TArray<uint8> Actual;
            Actual.SetNumUninitialized(Response.Num());
            DWORD Count = 0;
            TestTrue(TEXT("client reads the complete response"), ReadFile(Client, Actual.GetData(), Actual.Num(), &Count, nullptr) != 0);
            TestEqual(TEXT("complete response length"), Count, static_cast<DWORD>(Response.Num()));
            TestTrue(TEXT("all response bytes survive completion wait"), Actual == Response);
            CloseHandle(Client);
            Client = INVALID_HANDLE_VALUE;
        }
        else
        {
            Stopping = true;
            SetEvent(Stop);
        }
        const bool bJoined = Worker.WaitFor(FTimespan::FromSeconds(3));
        TestTrue(TEXT("peer close or stop releases completion wait"), bJoined);
        if (!bJoined)
        {
            // Keep test failure bounded even if a future regression loses stop.
            CloseHandle(Client);
            Client = INVALID_HANDLE_VALUE;
        }
        Worker.Wait();
        TestTrue(TEXT("worker completed its response write"), Worker.Get());
        if (Client != INVALID_HANDLE_VALUE)
        {
            CloseHandle(Client);
        }
        DisconnectNamedPipe(Pipe);
        for (HANDLE Handle : { Pipe, Io, Stop, Written })
        {
            CloseHandle(Handle);
        }
    }
    return true;
}
#endif
