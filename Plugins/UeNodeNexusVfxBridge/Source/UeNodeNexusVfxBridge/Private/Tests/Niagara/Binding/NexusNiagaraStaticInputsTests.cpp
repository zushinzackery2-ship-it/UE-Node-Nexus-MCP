#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraphPin.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputMetadata.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraStaticInput,
    "Nexus.Niagara.InputOwnership.StaticSwitchRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraStaticInput::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace VfxTranscode;
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    auto* Burst = LoadObject<UNiagaraScript>(nullptr,
        TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous"));
    if (!TestNotNull(TEXT("native template"), Template) || !TestNotNull(TEXT("native burst"), Burst))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    FNiagaraEmitterHandle& Emitter = System->GetEmitterHandles()[0];
    auto* Output = NiagaraModuleStack::ResolveOutputNode(&Emitter, TEXT("EmitterUpdateScript"));
    auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Burst, *Output);
    FInputBindingError Error;
    FNiagaraVariable Variable;
    if (!TestTrue(TEXT("unnamespaced static input resolves"),
        ResolveModuleInput(*Module, TEXT("UseLoopCountLimit"), Variable, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    auto* Metadata = Module->GetCalledGraph()->GetScriptVariable(Variable.GetName());
    TestTrue(TEXT("native metadata marks compile-time switch"), Metadata && Metadata->GetIsStaticSwitch());
    TestTrue(TEXT("shared catalog includes compile-time input"), IsAuthoredModuleInput(Variable, Metadata));
    TestEqual(TEXT("native static default is false"), ModuleInputDefaultText(Variable, Metadata), FString(TEXT("false")));
    FInputBindingContext Bindings;
    bool bChanged = false;
    AddExpectedError(TEXT("static_input_link_unsupported"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("compile-time switch rejects runtime handles during dry-run"),
        ApplyNativeInput(*System, Emitter, *Module, TEXT("UseLoopCountLimit"), TEXT("Engine.DeltaTime"),
            TEXT("ns_module_input_link"), true, Bindings, bChanged, Error));
    TestEqual(TEXT("static binding rejection"), Error.Code, FString(TEXT("static_input_link_unsupported")));
    TestFalse(TEXT("rejected static link never changes input"), bChanged);
    if (!TestTrue(TEXT("static local write uses native stack"),
        ApplyNativeInput(*System, Emitter, *Module, TEXT("UseLoopCountLimit"), TEXT("true"),
            TEXT("ns_module_input_set"), false, Bindings, bChanged, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    TestTrue(TEXT("compile-time value changed"), bChanged);
    auto* Pin = FindModuleOverridePin(*Module, Variable.GetName());
    if (!TestNotNull(TEXT("switch belongs to target function"), Pin))
    {
        return false;
    }
    TestEqual(TEXT("actual function switch enabled"), Pin->DefaultValue, FString(TEXT("true")));
    FString ExportError;
    const auto Json = ModuleJson(&Emitter, Output, Module, ExportError);
    TestTrue(TEXT("export succeeds"), Json.IsValid() && ExportError.IsEmpty());
    bool bExported = false;
    for (const auto& Value : Json->GetArrayField(TEXT("inputs")))
    {
        const auto Input = Value->AsObject();
        if (Input->GetStringField(TEXT("name")) == TEXT("UseLoopCountLimit"))
        {
            bExported = Input->GetBoolField(TEXT("static"))
                && Input->GetStringField(TEXT("value")) == TEXT("true")
                && Input->GetBoolField(TEXT("has_override"));
        }
    }
    TestTrue(TEXT("mirror preserves actual compile-time switch"), bExported);
    TestTrue(TEXT("static retry succeeds"), ApplyNativeInput(*System, Emitter, *Module,
        TEXT("UseLoopCountLimit"), TEXT("true"), TEXT("ns_module_input_set"), false, Bindings, bChanged, Error));
    TestFalse(TEXT("static retry remains unchanged"), bChanged);
    TestTrue(TEXT("static reset succeeds"), ApplyNativeInput(*System, Emitter, *Module,
        TEXT("UseLoopCountLimit"), FString(), TEXT("ns_module_input_reset"), false, Bindings, bChanged, Error));
    TestEqual(TEXT("native static reset restores default"), Pin->DefaultValue, FString(TEXT("false")));
    return true;
}
#endif
