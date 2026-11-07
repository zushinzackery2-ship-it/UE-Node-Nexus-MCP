#include "NexusNiagaraVectorFieldFixture.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace UeNodeNexusBridge::Tests
{
bool FNiagaraVectorFieldFixture::Initialize(FAutomationTestBase& Test, bool bWriterFirst)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    auto* SampleScript = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/Niagara/VectorFields/SampleVectorField.SampleVectorField"));
    auto* ForceScript = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/Niagara/VectorFields/ApplyVectorField.ApplyVectorField"));
    if (!Test.TestNotNull(TEXT("native emitter template"), Template)
        || !Test.TestNotNull(TEXT("native vector-field sampler"), SampleScript)
        || !Test.TestNotNull(TEXT("native vector-field force"), ForceScript))
    {
        return false;
    }
    System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    Emitter = &System->GetEmitterHandles()[0];
    Output = NiagaraModuleStack::ResolveOutputNode(Emitter, TEXT("ParticleUpdateScript"));
    if (!Test.TestNotNull(TEXT("native particle-update stack"), Output))
    {
        return false;
    }
    if (bWriterFirst)
    {
        Sample = FNiagaraStackGraphUtilities::AddScriptModuleToStack(SampleScript, *Output);
        Force = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ForceScript, *Output);
    }
    else
    {
        Force = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ForceScript, *Output);
        Sample = FNiagaraStackGraphUtilities::AddScriptModuleToStack(SampleScript, *Output);
    }
    return Test.TestNotNull(TEXT("native sample module"), Sample)
        && Test.TestNotNull(TEXT("native force module"), Force);
}

FString FNiagaraVectorFieldFixture::OutputHandle() const
{
    return TEXT("Output.") + Sample->GetFunctionName() + TEXT(".SampledVector");
}
}
#endif

