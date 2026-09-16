#include "State.h"
#include "Json.h"
#include "../Identity/Identity.h"

#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
static bool DefinitelyGone(const TSharedPtr<FJsonObject>& Identity)
{
    if (!Identity.IsValid())
    {
        return true;
    }
    double Number = 0;
    if (!Identity->TryGetNumberField(TEXT("pid"), Number) || Number <= 0 || Number > MAX_uint32)
    {
        return false;
    }
    const uint32 Pid = static_cast<uint32>(Number);
    HANDLE Process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, false, Pid);
    if (!Process)
    {
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }
    const bool bExited = WaitForSingleObject(Process, 0) == WAIT_OBJECT_0;
    CloseHandle(Process);
    const auto Current = ProcessIdentity(Pid);
    return bExited || (Current.IsValid() && !SameProcess(Identity));
}

bool Authenticate(const TSharedPtr<FJsonObject>& Payload, uint32 Peer)
{
    FString Secret, Instance;
    double Epoch = 0, Pid = 0;
    FString Created, Executable;
    const TSharedPtr<FJsonObject>* Identity = nullptr;
    if (!ReadString(Payload, TEXT("manager_secret"), Secret)
        || !ReadString(Payload, TEXT("instance_id"), Instance)
        || !ReadNumber(Payload, TEXT("manager_epoch"), Epoch)
        || !Payload->TryGetObjectField(TEXT("manager_identity"), Identity)
        || !ReadNumber(*Identity, TEXT("pid"), Pid) || Pid != Peer
        || !ReadString(*Identity, TEXT("process_created"), Created)
        || !ReadString(*Identity, TEXT("executable"), Executable)
        || Secret.IsEmpty() || Epoch < 1 || Epoch > 9007199254740991.0 || !FMath::IsFinite(Epoch))
    {
        return false;
    }
    FScopeLock Lock(&State().Mutex);
    auto& S = State();
    if (!S.Identity.IsValid() || Instance != S.Identity->GetStringField(TEXT("instance_id")))
    {
        return false;
    }
    if (Epoch == S.ManagerEpoch && Secret == S.ManagerSecret && S.Manager.IsValid())
    {
        return SameProcess(S.Manager);
    }
    if (!DefinitelyGone(S.Manager) || !SameProcess(*Identity))
    {
        return false;
    }
    const auto Record = ReadObject(S.Runtime / TEXT("manager.json"));
    const TSharedPtr<FJsonObject>* RecordedIdentity = nullptr;
    double RecordedEpoch = 0, RecordedPid = 0;
    FString RecordedSecret, RecordedCreated, RecordedExecutable;
    if (!Record.IsValid() || !Record->TryGetObjectField(TEXT("identity"), RecordedIdentity)
        || !Record->TryGetNumberField(TEXT("manager_epoch"), RecordedEpoch) || RecordedEpoch != Epoch
        || !Record->TryGetStringField(TEXT("manager_secret"), RecordedSecret) || RecordedSecret != Secret
        || !(*RecordedIdentity)->TryGetNumberField(TEXT("pid"), RecordedPid) || RecordedPid != Pid
        || !(*RecordedIdentity)->TryGetStringField(TEXT("process_created"), RecordedCreated) || RecordedCreated != Created
        || !(*RecordedIdentity)->TryGetStringField(TEXT("executable"), RecordedExecutable) || RecordedExecutable != Executable
        || Epoch <= S.ManagerEpoch)
    {
        return false;
    }
    S.Manager = *Identity;
    S.ManagerEpoch = static_cast<uint64>(Epoch);
    S.ManagerSecret = Secret;
    if (!S.bStopping)
    {
        S.bDraining = false;
        S.Prepared.Reset();
        ++S.Revision;
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard manager_adopted epoch=%llu pid=%u"), S.ManagerEpoch, Peer);
    return true;
}

static FString QuoteArgument(const FString& Value)
{
    FString Result = TEXT("\"");
    int32 Slashes = 0;
    for (TCHAR Ch : Value)
    {
        if (Ch == TEXT('\\'))
        {
            ++Slashes;
            continue;
        }
        Result += FString::ChrN(Slashes * (Ch == TEXT('"') ? 2 : 1), TEXT('\\'));
        if (Ch == TEXT('"'))
        {
            Result += TEXT('\\');
        }
        Result += Ch;
        Slashes = 0;
    }
    return Result + FString::ChrN(Slashes * 2, TEXT('\\')) + TEXT("\"");
}

void Watchdog()
{
    FString Runtime;
    {
        FScopeLock Lock(&State().Mutex);
        if (!State().bManaged || State().bStopping)
        {
            return;
        }
        Runtime = State().Runtime;
        const auto Record = ReadObject(Runtime / TEXT("manager.json"));
        const TSharedPtr<FJsonObject>* Identity = nullptr;
        if (Record.IsValid() && Record->TryGetObjectField(TEXT("identity"), Identity) && !DefinitelyGone(*Identity))
        {
            return;
        }
    }
    const auto Launcher = ReadObject(Runtime / TEXT("launcher.json"));
    const TArray<TSharedPtr<FJsonValue>>* Arguments = nullptr;
    if (!Launcher.IsValid() || !Launcher->TryGetArrayField(TEXT("command"), Arguments) || Arguments->Num() < 2)
    {
        return;
    }
    FString Parameters;
    for (int32 Index = 1; Index < Arguments->Num(); ++Index)
    {
        Parameters += QuoteArgument((*Arguments)[Index]->AsString()) + TEXT(" ");
    }
    FProcHandle Process = FPlatformProcess::CreateProc(*(*Arguments)[0]->AsString(), *Parameters,
        true, true, true, nullptr, 0, *Runtime, nullptr);
    FPlatformProcess::CloseProc(Process);
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard manager_restart_requested"));
}
}
