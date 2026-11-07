#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Niagara/Fixtures/NexusNiagaraVectorFieldFixture.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"

namespace
{
using namespace UeNodeNexusBridge;
using namespace VfxTranscode;
using Tests::FNiagaraVectorFieldFixture;

FString Inputs(FAutomationTestBase& Test, FNiagaraVectorFieldFixture& Fixture, UNiagaraNodeFunctionCall& Module)
{
    FString Error;
    const auto Export = ModuleJson(Fixture.Emitter, Fixture.Output, &Module, Error);
    if (!Test.TestTrue(TEXT("native inputs export"), Export.IsValid() && Error.IsEmpty()))
    {
        return FString();
    }
    FString Result;
    const auto Writer = TJsonWriterFactory<>::Create(&Result);
    Test.TestTrue(TEXT("native input snapshot serializes"), FJsonSerializer::Serialize(Export->GetArrayField(TEXT("inputs")), Writer));
    return Result;
}

TArray<FString> GraphIds(UNiagaraGraph& Graph)
{
    TArray<FString> Ids;
    for (const auto& Node : Graph.Nodes)
    {
        Ids.Add(Node->NodeGuid.ToString());
    }
    Ids.Sort();
    return Ids;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraModuleMovement,
    "Nexus.Niagara.ModuleMovement.PreserveNativeBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraModuleMovement::RunTest(const FString& Parameters)
{
    FNiagaraVectorFieldFixture Fixture;
    if (!Fixture.Initialize(*this, true))
    {
        return false;
    }
    FInputBindingError BindingError;
    bool bChanged = false;
    if (!TestTrue(TEXT("native position conversion binds"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), TEXT("Particles.Position"),
        TEXT("ns_module_input_convert"), false, Fixture.Bindings, bChanged, BindingError))
        || !TestTrue(TEXT("native preceding output binds"), ApplyNativeInput(
            *Fixture.System, *Fixture.Emitter, *Fixture.Force, TEXT("Vector Field Vector"), Fixture.OutputHandle(),
            TEXT("ns_module_input_link"), false, Fixture.Bindings, bChanged, BindingError)))
    {
        AddError(BindingError.Message);
        return false;
    }
    auto* Local = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Fixture.Force->FunctionScript, *Fixture.Output);
    if (!TestNotNull(TEXT("native local-value force module"), Local))
    {
        return false;
    }
    FNiagaraVariable LocalInput;
    if (!TestTrue(TEXT("local vector input resolves"), ResolveModuleInput(*Local, TEXT("Vector Field Vector"), LocalInput, BindingError))
        || !TestTrue(TEXT("non-default local vector writes natively"), ApplyNativeInput(
            *Fixture.System, *Fixture.Emitter, *Local, TEXT("Vector Field Vector"), TEXT("(X=2.25,Y=-4.5,Z=7.75)"),
            TEXT("ns_module_input_set"), false, Fixture.Bindings, bChanged, BindingError)))
    {
        AddError(BindingError.Message);
        return false;
    }
    const FName LocalOverrideName(*(Local->GetFunctionName() + TEXT(".Vector Field Vector")));
    UEdGraphPin* LocalOverride = FindModuleOverridePin(*Local, LocalOverrideName);
    if (!TestNotNull(TEXT("default-linked vector writes a graph override"), LocalOverride)
        || !TestEqual(TEXT("local graph override is unlinked"), LocalOverride->LinkedTo.Num(), 0))
    {
        return false;
    }
    const FString LocalOverrideValue = LocalOverride->DefaultValue;
    auto* GravityScript = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce"));
    if (!TestNotNull(TEXT("native gravity module with a local default"), GravityScript))
    {
        return false;
    }
    auto* RapidModule = FNiagaraStackGraphUtilities::AddScriptModuleToStack(GravityScript, *Fixture.Output);
    if (!TestNotNull(TEXT("native rapid-iteration module"), RapidModule))
    {
        return false;
    }
    FNiagaraVariable RapidInput;
    if (!TestTrue(TEXT("gravity input resolves"), ResolveModuleInput(*RapidModule, TEXT("Gravity"), RapidInput, BindingError)))
    {
        AddError(BindingError.Message);
        return false;
    }
    auto* RapidStackInput = Fixture.Bindings.Resolve(*Fixture.System, *Fixture.Emitter, *RapidModule, RapidInput, BindingError);
    if (!TestNotNull(TEXT("native gravity stack input resolves"), RapidStackInput)
        || !TestTrue(TEXT("gravity has a local default"), RapidStackInput->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local)
        || !TestTrue(TEXT("non-default gravity writes natively"), ApplyNativeInput(
            *Fixture.System, *Fixture.Emitter, *RapidModule, TEXT("Gravity"), TEXT("(X=2.25,Y=-4.5,Z=7.75)"),
            TEXT("ns_module_input_set"), false, Fixture.Bindings, bChanged, BindingError)))
    {
        AddError(BindingError.Message);
        return false;
    }
    const FString SampleInputs = Inputs(*this, Fixture, *Fixture.Sample);
    const FString ForceInputs = Inputs(*this, Fixture, *Fixture.Force);
    const FString LocalInputs = Inputs(*this, Fixture, *Local);
    const FString RapidInputs = Inputs(*this, Fixture, *RapidModule);
    const FGuid SampleId = Fixture.Sample->NodeGuid;
    const FGuid ForceId = Fixture.Force->NodeGuid;
    const auto NodeIds = GraphIds(*Fixture.Output->GetNiagaraGraph());
    const auto* Data = Fixture.Emitter->GetInstance().GetEmitterData();
    UNiagaraScript* Script = Data ? Data->UpdateScriptProps.Script : nullptr;
    if (!TestNotNull(TEXT("native particle-update script"), Script))
    {
        return false;
    }
    TArray<FNiagaraVariable> Rapid;
    Script->RapidIterationParameters.GetParameters(Rapid);
    const FVector3f ExpectedLocal(2.25f, -4.5f, 7.75f);
    bool bLocalRapid = false;
    for (FNiagaraVariable& Variable : Rapid)
    {
        const uint8* Bytes = Script->RapidIterationParameters.GetParameterData(Variable);
        if (Bytes)
        {
            bLocalRapid |= Variable.GetName().ToString().Contains(RapidModule->GetFunctionName())
                && Variable.GetType() == RapidInput.GetType() && Variable.GetSizeInBytes() == sizeof(ExpectedLocal)
                && FMemory::Memcmp(Bytes, &ExpectedLocal, sizeof(ExpectedLocal)) == 0;
            Variable.SetData(Bytes);
        }
    }
    if (!TestTrue(TEXT("non-default gravity has real allocated RI bytes"), bLocalRapid))
    {
        return false;
    }
    AddInfo(FString::Printf(TEXT("Native storage graph_override=%s rapid_script=%s rapid_count=%d vector=(2.25,-4.5,7.75)"),
        *LocalOverrideValue, *Script->GetPathName(), Rapid.Num()));
    TArray<UNiagaraNodeFunctionCall*> Expected;
    NiagaraModuleStack::GetOrderedModules(Fixture.Output, Expected);
    const int32 Count = Expected.Num();
    if (!TestTrue(TEXT("fixture contains default and added modules"), Count >= 4))
    {
        return false;
    }
    const TArray<TPair<UNiagaraNodeFunctionCall*, int32>> Moves =
    {
        { Fixture.Sample, 0 },
        { Fixture.Force, 1 },
        { Fixture.Force, Count - 1 },
        { Fixture.Sample, 1 },
        { Fixture.Sample, 1 },
        { Local, 0 },
        { Local, Count - 1 },
        { Local, Count - 1 },
        { RapidModule, 0 },
        { RapidModule, Count - 1 },
        { RapidModule, Count - 1 }
    };
    for (const auto& Move : Moves)
    {
        const int32 BeforeIndex = Expected.IndexOfByKey(Move.Key);
        Expected.RemoveAt(BeforeIndex);
        Expected.Insert(Move.Key, Move.Value);
        FString Error;
        if (!TestTrue(TEXT("native stack movement succeeds"), NiagaraModuleStack::MoveModuleInNativeStack(
            *Fixture.System, *Fixture.Emitter, *Fixture.Output, *Move.Key, Move.Value, Fixture.Bindings, bChanged, Error)))
        {
            AddError(Error);
            return false;
        }
        TestEqual(TEXT("reported movement agrees with final index"), bChanged, BeforeIndex != Move.Value);
        TArray<UNiagaraNodeFunctionCall*> After;
        NiagaraModuleStack::GetOrderedModules(Fixture.Output, After);
        TestTrue(TEXT("complete module order readback"), After == Expected);
        TestTrue(TEXT("sampler and force node identities remain"), Fixture.Sample->NodeGuid == SampleId && Fixture.Force->NodeGuid == ForceId);
        TestTrue(TEXT("all override and converter node identities remain"), GraphIds(*Fixture.Output->GetNiagaraGraph()) == NodeIds);
        TestEqual(TEXT("position conversion remains"), Inputs(*this, Fixture, *Fixture.Sample), SampleInputs);
        TestEqual(TEXT("preceding output binding remains"), Inputs(*this, Fixture, *Fixture.Force), ForceInputs);
        TestEqual(TEXT("non-default local input remains"), Inputs(*this, Fixture, *Local), LocalInputs);
        TestTrue(TEXT("graph local value and override pin identity remain"), FindModuleOverridePin(*Local, LocalOverrideName) == LocalOverride
            && LocalOverride->DefaultValue == LocalOverrideValue);
        TestEqual(TEXT("non-default rapid-iteration input remains"), Inputs(*this, Fixture, *RapidModule), RapidInputs);
        TArray<FNiagaraVariable> AfterRapid;
        Script->RapidIterationParameters.GetParameters(AfterRapid);
        TestEqual(TEXT("rapid-iteration parameter count remains"), AfterRapid.Num(), Rapid.Num());
        for (const FNiagaraVariable& Variable : Rapid)
        {
            const uint8* Bytes = Script->RapidIterationParameters.GetParameterData(Variable);
            TestTrue(TEXT("rapid-iteration type and bytes remain"), AfterRapid.Contains(Variable)
                && (Variable.IsDataAllocated() ? Bytes && FMemory::Memcmp(Bytes, Variable.GetData(), Variable.GetSizeInBytes()) == 0 : !Bytes));
        }
    }
    AddExpectedError(TEXT("module index is outside the native stack"), EAutomationExpectedErrorFlags::Contains, 2);
    for (const int32 Invalid : { -1, Count })
    {
        FString Error;
        TestFalse(TEXT("out-of-range movement is rejected"), NiagaraModuleStack::MoveModuleInNativeStack(
            *Fixture.System, *Fixture.Emitter, *Fixture.Output, *Fixture.Sample, Invalid, Fixture.Bindings, bChanged, Error));
        TestFalse(TEXT("invalid movement reports no change"), bChanged);
        TArray<UNiagaraNodeFunctionCall*> After;
        NiagaraModuleStack::GetOrderedModules(Fixture.Output, After);
        TestTrue(TEXT("invalid movement preserves complete order"), After == Expected);
        TestTrue(TEXT("invalid movement preserves input groups"), GraphIds(*Fixture.Output->GetNiagaraGraph()) == NodeIds);
    }
    return true;
}
#endif

