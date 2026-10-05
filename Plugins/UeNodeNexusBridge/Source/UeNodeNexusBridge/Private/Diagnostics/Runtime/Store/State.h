#pragma once

#include "../NexusRuntimeDiagnostics.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics::Store
{
constexpr int32 MaxEntries = 512;
constexpr int32 MaxBytes = 2 * 1024 * 1024;
constexpr int32 MaxSamples = 3;

struct FCounts
{
    int64 Errors = 0;
    int64 Warnings = 0;
};

struct FSession
{
    FString Id;
    int64 Errors = 0;
    int64 Warnings = 0;
    int64 Dropped = 0;
    int64 DroppedThrough = 0;
    bool bPie = false;
    bool bActive = true;
    bool bSourcesComplete = false;
    bool bCounterOverflow = false;
    TMap<FString, FCounts> Assets;
    FCounts Unattributed;
    TArray<TSharedPtr<FJsonObject>> Samples;
};

struct FEntry
{
    FString Key;
    TSharedPtr<FJsonObject> Value;
    int32 Bytes = 0;
};

struct FState
{
    FCriticalSection Mutex;
    TArray<FSession> Sessions;
    TArray<FEntry> Entries;
    TMap<FString, TSharedPtr<FJsonObject>> Aggregates;
    FString Instance;
    int32 Bytes = 0;
    int64 Sequence = 0;
    bool bCaptureReady = false;

    FState();
};

FState& State();
FString Digest(const FString& Text);
FSession* FindSession(FState& Data, const FString& Id);
const FSession* DefaultSession(const FState& Data);
TSharedPtr<FJsonObject> SessionJson(const FState& Data, const FSession& Session);
void Evict(FState& Data);
void UpdateSamples(FSession& Session, const TSharedPtr<FJsonObject>& Event);
void RebuildSamples(const FState& Data, FSession& Session);
TArray<TSharedPtr<FJsonValue>> SampleJson(const FSession& Session);
}
