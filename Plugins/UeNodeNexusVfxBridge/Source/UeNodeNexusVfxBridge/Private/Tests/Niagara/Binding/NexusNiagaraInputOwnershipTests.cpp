#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraphPin.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/StructOnScope.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraInputOwnership,
    "Nexus.Niagara.InputOwnership.DownstreamAliasAndNativeWrite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraInputOwnership::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace VfxTranscode;
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    auto* Burst = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
    if (!TestNotNull(TEXT("engine template"), Template) || !TestNotNull(TEXT("native burst"), Burst))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    FNiagaraEmitterHandle& Emitter = System->GetEmitterHandles()[0];
    auto* Output = NiagaraModuleStack::ResolveOutputNode(&Emitter, TEXT("EmitterUpdateScript"));
    auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Burst, *Output);
    auto* Later = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Burst, *Output);
    FInputBindingError Error;
    FNiagaraVariable Variable;
    if (!TestTrue(TEXT("resolve native typed count"), ResolveModuleInput(*Module, TEXT("Spawn Count"), Variable, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    const FNiagaraParameterHandle Alias = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
        FNiagaraParameterHandle(Variable.GetName()), Module);
    UEdGraphPin& Stale = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
        *Later, Alias, Variable.GetType(), FGuid(), FGuid());
    Stale.DefaultValue = TEXT("3");
    TestNull(TEXT("downstream duplicate alias is not a target override"),
        FindModuleOverridePin(*Module, Alias.GetParameterHandleString()));
    FInputBindingContext Bindings;
    bool bChanged = false;
    if (!TestTrue(TEXT("native local write repairs target despite equal stale text"),
        ApplyNativeInput(*System, Emitter, *Module, TEXT("Spawn Count"), TEXT("3"),
            TEXT("ns_module_input_set"), false, Bindings, bChanged, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    TestTrue(TEXT("real target changed from native default one"), bChanged);
    auto* Input = Bindings.Resolve(*System, Emitter, *Module, Variable, Error);
    if (!TestNotNull(TEXT("native target input readback"), Input))
    {
        return false;
    }
    const auto Value = Input->GetLocalValueStruct();
    TestEqual(TEXT("native executable input is three"),
        *reinterpret_cast<const int32*>(Value->GetStructMemory()), 3);
    TestEqual(TEXT("unrelated downstream pin is preserved"), Stale.DefaultValue, FString(TEXT("3")));
    TestTrue(TEXT("unchanged native retry succeeds"), ApplyNativeInput(*System, Emitter, *Module,
        TEXT("Spawn Count"), TEXT("3"), TEXT("ns_module_input_set"), false, Bindings, bChanged, Error));
    TestFalse(TEXT("retry reports actual unchanged target"), bChanged);
    TestTrue(TEXT("reset uses native default binding"), ApplyNativeInput(*System, Emitter, *Module,
        TEXT("Spawn Count"), TEXT("1"), TEXT("ns_module_input_reset"), false, Bindings, bChanged, Error));
    Input = Bindings.Resolve(*System, Emitter, *Module, Variable, Error);
    TestFalse(TEXT("native reset is complete"), Input->CanReset());
    Bindings.ViewModel.Reset();
    FString RemovalError;
    TestTrue(TEXT("module deletion uses native stack"),
        NiagaraModuleStack::RemoveModuleFromNativeStack(*System, Emitter, *Later, RemovalError));
    TArray<UNiagaraNodeFunctionCall*> Modules;
    NiagaraModuleStack::GetOrderedModules(Output, Modules);
    TestFalse(TEXT("deleted module absent from real stack"), Modules.Contains(Later));
    return true;
}
#endif
