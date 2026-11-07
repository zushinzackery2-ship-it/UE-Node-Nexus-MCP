#include "NexusNiagaraCompilation.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraModule.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "RenderingThread.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogNexusNiagaraCompilation, Log, All);

namespace UeNodeNexusBridge::NiagaraCompilation
{
bool Runnable(UNiagaraSystem* System)
{
    return System && System->IsValid() && System->IsReadyToRun()
        && !System->HasOutstandingCompilationRequests(true) && !System->NeedsRequestCompile();
}

void AppendStatus(UNiagaraSystem* System, const TSharedPtr<FJsonObject>& Data)
{
    Data->SetBoolField(TEXT("asset_valid"), System && System->IsValid());
    Data->SetBoolField(TEXT("compilation_finished"), System && !System->HasOutstandingCompilationRequests(true));
    Data->SetBoolField(TEXT("ready_to_run"), Runnable(System));
    Data->SetBoolField(TEXT("needs_compile"), System && System->NeedsRequestCompile());
    if (System && !Runnable(System))
    {
        Data->SetArrayField(TEXT("compile_scripts"), Diagnostics(System));
    }
}

bool Request(UNiagaraSystem* System)
{
    check(IsInGameThread());
    const bool bSuperseded = System->HasActiveCompilations();
    // A superseded request can still prepare shared rapid-iteration parameters
    // while polling, invalidating the compile IDs captured by the new request.
    System->KillAllActiveCompilations();
    const bool bRequested = System->RequestCompile(true);
    UE_LOG(LogNexusNiagaraCompilation, Log, TEXT("event=request asset=%s superseded=%d requested=%d active=%d"),
        *System->GetPathName(), bSuperseded, bRequested, System->HasActiveCompilations());
    return bRequested;
}

bool Wait(UNiagaraSystem* System)
{
    check(IsInGameThread());
    const double Started = FPlatformTime::Seconds();
    // UE collects GPU scripts before integrating CPU results. Integrate first,
    // then dispatch the newly produced GPU queue and wait for its shader maps.
    System->WaitForCompilationComplete(false, false);
    INiagaraModule::Get().ProcessShaderCompilationQueue();
    System->WaitForCompilationComplete(true, false);
    FlushRenderingCommands();
    const bool bReady = Runnable(System);
    UE_LOG(LogNexusNiagaraCompilation, Log, TEXT("event=complete asset=%s ready=%d pending=%d spawn=%d update=%d seconds=%.6f"),
        *System->GetPathName(), bReady, System->HasOutstandingCompilationRequests(true),
        static_cast<int32>(System->GetSystemSpawnScript()->GetLastCompileStatus()),
        static_cast<int32>(System->GetSystemUpdateScript()->GetLastCompileStatus()), FPlatformTime::Seconds() - Started);
    return bReady;
}

TArray<TSharedPtr<FJsonValue>> Diagnostics(UNiagaraSystem* System)
{
    TArray<UNiagaraScript*> Scripts;
    Scripts.Add(System->GetSystemSpawnScript());
    Scripts.Add(System->GetSystemUpdateScript());
    for (const auto& Handle : System->GetEmitterHandles())
    {
        if (const auto* Data = Handle.GetEmitterData())
        {
            Data->GetScripts(Scripts, false);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const auto* Script : Scripts)
    {
        if (!Script)
        {
            continue;
        }
        const auto& Executable = Script->GetVMExecutableData();
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("script"), Script->GetPathName());
        Row->SetStringField(TEXT("compile_status"), StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue(
            static_cast<int64>(Script->GetLastCompileStatus())));
        Row->SetStringField(TEXT("error_message"), Executable.ErrorMsg.Left(4096));
        TArray<TSharedPtr<FJsonValue>> Events;
        for (const auto& Event : Executable.LastCompileEvents)
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("message"), Event.Message.Left(4096));
            Item->SetNumberField(TEXT("severity"), static_cast<int32>(Event.Severity));
            Events.Add(MakeShared<FJsonValueObject>(Item));
        }
        Row->SetArrayField(TEXT("events"), Events);
        Result.Add(MakeShared<FJsonValueObject>(Row));
    }
    return Result;
}
}
