#include "NexusNiagaraCompilation.h"

#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraModule.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "RenderingThread.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::NiagaraCompilation
{
bool Wait(UNiagaraSystem* System)
{
    check(IsInGameThread());
    // UE collects GPU scripts before integrating CPU results. Integrate first,
    // then dispatch the newly produced GPU queue and wait for its shader maps.
    System->WaitForCompilationComplete(false, false);
    INiagaraModule::Get().ProcessShaderCompilationQueue();
    System->WaitForCompilationComplete(true, false);
    FlushRenderingCommands();
    return System->IsReadyToRun();
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
