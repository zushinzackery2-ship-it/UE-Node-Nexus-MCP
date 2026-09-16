#include "Identity.h"
#include "../Control/State.h"

#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
static HANDLE ProjectLock = INVALID_HANDLE_VALUE;
static FString RecordPath;

static bool RejectStartup(uint8 ExitCode, const FString& Diagnostic)
{
    UE_LOG(LogTemp, Error, TEXT("Nexus Guard: %s"), *Diagnostic);
    if (GLog)
    {
        // UE 5.5 may redirect on a dedicated thread; Flush alone need not drain it.
        // This process is rejected before editor initialization, so transfer logging
        // ownership just as engine teardown does before its final synchronous flush.
        GLog->SetCurrentThreadAsPrimaryThread();
        GLog->Flush();
    }
    FPlatformMisc::RequestExitWithStatus(true, ExitCode);
    return false;
}

bool InitializeIdentity()
{
    FString Commandlet;
    if (FParse::Value(FCommandLine::Get(), TEXT("run="), Commandlet)
        || FParse::Param(FCommandLine::Get(), TEXT("cook"))
        || FParse::Param(FCommandLine::Get(), TEXT("commandlet")))
    {
        return false;
    }
    const FString Project = CanonicalPath(FPaths::GetProjectFilePath());
    const FString Key = Sha256(Project);
    if (Project.IsEmpty() || Key.IsEmpty())
    {
        return RejectStartup(74, TEXT("identity_unverified: project identity could not be resolved"));
    }
    // This lock domain is deliberately independent of a test/custom Broker root.
    const FString LockPath = DefaultRuntime() / TEXT("ProjectLocks") / (Key + TEXT(".lock"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(LockPath), true);
    ProjectLock = CreateFileW(*LockPath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    OVERLAPPED Overlapped;
    FMemory::Memzero(Overlapped);
    if (ProjectLock == INVALID_HANDLE_VALUE || !LockFileEx(ProjectLock,
        LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &Overlapped))
    {
        const DWORD Error = GetLastError();
        const bool bDuplicate = Error == ERROR_LOCK_VIOLATION || Error == ERROR_SHARING_VIOLATION;
        return RejectStartup(bDuplicate ? 73 : 74,
            FString::Printf(TEXT("%s project=%s key=%s lock=%s error=%u"),
                bDuplicate ? TEXT("duplicate_project") : TEXT("identity_unverified"), *Project, *Key, *LockPath, Error));
    }
    auto& S = State();
    S.Identity = ProcessIdentity(GetCurrentProcessId());
    if (!S.Identity.IsValid())
    {
        return RejectStartup(74, FString::Printf(TEXT("identity_unverified: process identity failed project=%s key=%s"),
            *Project, *Key));
    }
    FString Instance, Intent;
    FParse::Value(FCommandLine::Get(), TEXT("NexusInstance="), Instance);
    FParse::Value(FCommandLine::Get(), TEXT("NexusIntent="), Intent);
    if (Instance.IsEmpty())
    {
        Instance = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    }
    S.Runtime = DefaultRuntime();
    FParse::Value(FCommandLine::Get(), TEXT("NexusRuntime="), S.Runtime);
    S.bManaged = !Intent.IsEmpty();
    S.Identity->SetStringField(TEXT("instance_id"), Instance);
    S.Identity->SetStringField(TEXT("start_intent_id"), Intent);
    S.Identity->SetStringField(TEXT("project_key"), Key);
    S.Identity->SetStringField(TEXT("project_path"), Project);
    S.Identity->SetStringField(TEXT("project_name"), FPaths::GetBaseFilename(Project));
    S.Identity->SetStringField(TEXT("engine_dir"), CanonicalPath(FPaths::EngineDir(), true));
    S.Identity->SetStringField(TEXT("launch_profile"), FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) ? TEXT("offscreen") : TEXT("interactive"));
    S.Identity->SetStringField(TEXT("rhi"), FParse::Param(FCommandLine::Get(), TEXT("nullrhi")) ? TEXT("nullrhi")
        : (FParse::Param(FCommandLine::Get(), TEXT("dx11")) || FParse::Param(FCommandLine::Get(), TEXT("d3d11"))) ? TEXT("d3d11") : TEXT("d3d12"));
    S.Identity->SetNumberField(TEXT("guard_protocol"), 1);
    S.Identity->SetNumberField(TEXT("contract_version"), 4);
    S.Identity->SetObjectField(TEXT("guard_build"), BuildIdentity());
    S.Identity->SetStringField(TEXT("runtime_dir"), S.Runtime);
    RecordPath = S.Runtime / TEXT("Instances") / (LexToString(GetCurrentProcessId()) + TEXT(".json"));
    if (!WriteAtomic(RecordPath, S.Identity))
    {
        return RejectStartup(74, FString::Printf(TEXT("identity_unverified: discovery record write failed: %s"), *RecordPath));
    }
    const FString Record = Encode(S.Identity);
    FTCHARToUTF8 Utf8(*Record);
    SetFilePointer(ProjectLock, 1, nullptr, FILE_BEGIN);
    DWORD Written = 0;
    WriteFile(ProjectLock, Utf8.Get(), Utf8.Length(), &Written, nullptr);
    SetEndOfFile(ProjectLock);
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard phase=PostConfigInit project_key=%s instance_id=%s managed=%d"), *Key, *Instance, S.bManaged);
    return true;
}

void ReleaseIdentity()
{
    if (!RecordPath.IsEmpty())
    {
        IFileManager::Get().Delete(*RecordPath);
    }
    if (ProjectLock != INVALID_HANDLE_VALUE)
    {
        CloseHandle(ProjectLock);
        ProjectLock = INVALID_HANDLE_VALUE;
    }
}
}
