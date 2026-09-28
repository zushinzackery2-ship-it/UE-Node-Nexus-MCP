#include "Monitor.h"
#include "Dialogs.h"
#include "../Control/State.h"

#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle::Startup
{
// The latest log line tells a long load from a stall: a moving timestamp is
// progress, a still one with no dialog is a hang worth reporting.
class FLatestLine final : public FOutputDevice
{
public:
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        const int32 Length = FMath::Min(FCString::Strlen(Text), 300);
        FScopeLock Lock(&Mutex);
        Line = FString::ConstructFromPtrSize(Text, Length);
        LineCategory = Category;
        At = FDateTime::UtcNow();
    }

    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

    virtual bool CanBeUsedOnMultipleThreads() const override
    {
        return true;
    }

    TSharedPtr<FJsonObject> Json()
    {
        FScopeLock Lock(&Mutex);
        if (Line.IsEmpty())
        {
            return nullptr;
        }
        auto Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("at"), At.ToIso8601());
        Result->SetStringField(TEXT("category"), LineCategory.ToString());
        Result->SetStringField(TEXT("text"), Line);
        return Result;
    }

private:
    FCriticalSection Mutex;
    FString Line;
    FName LineCategory;
    FDateTime At;
};

struct FProgress
{
    FString Phase = TEXT("engine_init");
    FDateTime PhaseAt = FDateTime::UtcNow();
    FString Window;
    TUniquePtr<FLatestLine> Latest;
    FDelegateHandle Modules;
    FDelegateHandle EngineInit;
    FDelegateHandle LoopInit;
};

static FProgress& Progress()
{
    static FProgress Value;
    return Value;
}

// ``hidden`` when the launcher asked for SW_HIDE (Start-Process -WindowStyle
// Hidden): the first window, a startup prompt included, then never shows.
static FString LaunchWindow()
{
    if (FParse::Param(FCommandLine::Get(), TEXT("RenderOffscreen")) || FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
    {
        return TEXT("offscreen");
    }
    STARTUPINFOW Info;
    FMemory::Memzero(Info);
    Info.cb = sizeof(Info);
    GetStartupInfoW(&Info);
    return (Info.dwFlags & STARTF_USESHOWWINDOW) != 0 && Info.wShowWindow == SW_HIDE ? TEXT("hidden") : TEXT("normal");
}

void Install(bool bManaged)
{
    FProgress& P = Progress();
    P.Window = LaunchWindow();
    P.Latest = MakeUnique<FLatestLine>();
    GLog->AddOutputDevice(P.Latest.Get());
    Dialogs::Install(bManaged || P.Window != TEXT("normal"));
    P.Modules = FModuleManager::Get().OnModulesChanged().AddLambda([](FName, EModuleChangeReason)
    {
        Dialogs::Wrap();
    });
    P.EngineInit = FCoreDelegates::OnPostEngineInit.AddLambda([]()
    {
        Dialogs::Wrap();
        EnterPhase(TEXT("post_engine_init"));
    });
    P.LoopInit = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda([]()
    {
        EnterPhase(TEXT("loop_init_complete"));
    });
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard startup window=%s acknowledge_advisories=%d"), *P.Window,
        bManaged || P.Window != TEXT("normal"));
}

void Uninstall()
{
    FProgress& P = Progress();
    FModuleManager::Get().OnModulesChanged().Remove(P.Modules);
    FCoreDelegates::OnPostEngineInit.Remove(P.EngineInit);
    FCoreDelegates::OnFEngineLoopInitComplete.Remove(P.LoopInit);
    Dialogs::Uninstall();
    if (P.Latest.IsValid())
    {
        GLog->RemoveOutputDevice(P.Latest.Get());
        P.Latest.Reset();
    }
}

void EnterPhase(const TCHAR* Phase)
{
    FScopeLock Lock(&State().Mutex);
    Progress().Phase = Phase;
    Progress().PhaseAt = FDateTime::UtcNow();
}

void AppendStatus(const TSharedPtr<FJsonObject>& Status)
{
    FProgress& P = Progress();
    auto Startup = MakeShared<FJsonObject>();
    Startup->SetStringField(TEXT("phase"), P.Phase);
    Startup->SetStringField(TEXT("phase_at"), P.PhaseAt.ToIso8601());
    Startup->SetStringField(TEXT("launch_window"), P.Window);
    if (const TSharedPtr<FJsonObject> Latest = P.Latest.IsValid() ? P.Latest->Json() : nullptr)
    {
        Startup->SetObjectField(TEXT("last_log"), Latest);
    }
    Status->SetObjectField(TEXT("startup_progress"), Startup);
    Dialogs::AppendStatus(Status);
}

TSharedPtr<FJsonObject> OpenDialogLocked()
{
    return Dialogs::OpenLocked();
}
}
