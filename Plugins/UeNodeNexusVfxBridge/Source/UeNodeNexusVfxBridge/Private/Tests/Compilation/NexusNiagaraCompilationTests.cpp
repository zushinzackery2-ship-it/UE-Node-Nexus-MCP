#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Niagara/Compilation/NexusNiagaraCompilation.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UeNodeNexusNiagaraOps.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraGpuCompilation,
    "Nexus.Issues7.Niagara.GpuCompilationReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraGpuCompilation::RunTest(const FString& Parameters)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    if (!TestNotNull(TEXT("engine emitter template is available"), Template))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    auto* Data = System->GetEmitterHandles()[0].GetInstance().GetEmitterData();
    Data->SimTarget = ENiagaraSimTarget::GPUComputeSim;
    System->RequestCompile(true);
    if (!TestTrue(TEXT("CPU integration, GPU dispatch and shader completion produce a runnable system"), NiagaraCompilation::Wait(System)))
    {
        for (const auto& Value : NiagaraCompilation::Diagnostics(System))
        {
            AddInfo(Value->AsObject()->GetStringField(TEXT("script")));
        }
        return false;
    }
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), System->GetPathName());
    Payload->SetBoolField(TEXT("wait"), true);
    const auto Response = HandleNiagaraCompile(TEXT("niagara_compile"), TEXT("gpu-readiness"), Payload);
    TestTrue(TEXT("public synchronous compile succeeds"), Response->GetBoolField(TEXT("ok")));
    const auto Result = Response->GetObjectField(TEXT("data"));
    TestTrue(TEXT("public synchronous compile is ready"), Result->GetBoolField(TEXT("ready_to_run")));
    TestEqual(TEXT("public compile state is explicit"), Result->GetStringField(TEXT("compile_state")), FString(TEXT("ready")));
    // A finished script with failed executable data is not runnable. GPU
    // emitter CPU bytecode is deliberately not used as this oracle.
    auto& Executable = System->GetSystemUpdateScript()->GetVMExecutableData();
    Executable.ByteCode.Reset();
    Executable.OptimizedByteCode.Reset();
#if VECTORVM_SUPPORTS_EXPERIMENTAL
    Executable.ExperimentalContextData.Reset();
#endif
    Executable.LastCompileStatus = ENiagaraScriptCompileStatus::NCS_Error;
    Executable.ErrorMsg = TEXT("Nexus fixture: finished compilation failed");
    TestTrue(TEXT("engine readiness alone still reports completion"), System->IsReadyToRun());
    TestFalse(TEXT("engine validity rejects failed executable"), System->IsValid());
    TestFalse(TEXT("Nexus readiness includes actual validity"), NiagaraCompilation::Runnable(System));
    auto Status = MakeShared<FJsonObject>();
    NiagaraCompilation::AppendStatus(System, Status);
    TestFalse(TEXT("summary rejects the invalid finished system"), Status->GetBoolField(TEXT("ready_to_run")));
    TestTrue(TEXT("failure retains script diagnostics"), Status->GetArrayField(TEXT("compile_scripts")).Num() > 0);
    return true;
}
#endif
