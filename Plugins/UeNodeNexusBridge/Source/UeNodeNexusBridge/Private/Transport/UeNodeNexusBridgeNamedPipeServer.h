#pragma once

#include "CoreMinimal.h"

// Local-only IPC transport: a Windows named pipe server that each editor
// instance exposes at \\.\pipe\UeNodeNexusBridge.<pid>. Replaces the former
// fixed-TCP-port HTTP server, eliminating port collisions on rapid editor
// toggling and enabling multiple concurrent editor instances (the MCP client
// discovers and selects an instance by pid).
class FUeNodeNexusBridgeNamedPipeServer
{
public:
    // Both special members are out-of-line: the TUniquePtr<FWorker> member must
    // only have its destructor instantiated where FWorker is complete (this
    // .cpp), never in translation units that merely include this header.
    FUeNodeNexusBridgeNamedPipeServer();
    ~FUeNodeNexusBridgeNamedPipeServer();

    void Start();
    void Stop();

private:
    // Each worker owns one pipe instance of the shared name and serves one
    // client connection at a time (accept -> dispatch -> respond -> loop).
    class FWorker;

    TArray<TUniquePtr<FWorker>> Workers;
    void* StopEvent = nullptr;   // Win32 manual-reset HANDLE shared by all workers
};
