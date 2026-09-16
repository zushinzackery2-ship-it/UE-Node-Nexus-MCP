#pragma once

#include "NexusLifecycle.h"
#include "HAL/CriticalSection.h"

namespace NexusLifecycle
{
struct FRequest
{
    FString ScopeId;
    FString Outcome;
    bool bRunning = false;
    bool bFinished = false;
};

struct FGuardState
{
    FCriticalSection Mutex;
    TSharedPtr<FJsonObject> Identity;
    TSharedPtr<FJsonObject> Editor = MakeShared<FJsonObject>();
    TSharedPtr<FJsonObject> Manager;
    FString Runtime;
    FString ManagerSecret;
    uint64 ManagerEpoch = 0;
    uint64 Revision = 1;
    uint64 ContextEpoch = 1;
    bool bReady = false;
    bool bDraining = false;
    bool bStopping = false;
    bool bCommitQueued = false;
    bool bManaged = false;
    FString CloseId;
    TSharedPtr<FJsonObject> Prepared;
    TMap<FString, TSharedPtr<FJsonObject>> Grants;
    TMap<FString, FRequest> Requests;
    TArray<FString> Finished;
    FInspector Inspector;
};

FGuardState& State();
TSharedPtr<FJsonObject> StatusLocked();
TSharedPtr<FJsonObject> Failure(const FString& Code, const FString& Message);
TSharedPtr<FJsonObject> Success(const TSharedPtr<FJsonObject>& Data);
TSharedPtr<FJsonObject> Dispatch(const TSharedPtr<FJsonObject>& Request, uint32 Peer);
TSharedPtr<FJsonObject> Close(const FString& Operation, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> ScopeStatus(const FString& ScopeId);
bool Authenticate(const TSharedPtr<FJsonObject>& Payload, uint32 Peer);
void Watchdog();
}
