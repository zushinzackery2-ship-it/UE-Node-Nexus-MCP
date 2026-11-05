#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::VfxTranscode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraEnumParameter,
    "Nexus.Niagara.Transcode.EnumParameterRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraEnumParameter::RunTest(const FString& Parameters)
{
    const TCHAR* Names[] = { TEXT("ENiagaraEmitterLifeCycleMode"), TEXT("ENiagara_ColorInitializationMode"),
        TEXT("ENiagara_LifetimeMode"), TEXT("ENiagara_MassInitializationMode"),
        TEXT("ENiagara_PositionInitializationMode"), TEXT("ENiagara_SizeScaleMode") };
    for (const TCHAR* Name : Names)
    {
        const FString Path = FString::Printf(TEXT("/Niagara/Enums/%s.%s"), Name, Name);
        UEnum* Enum = LoadObject<UEnum>(nullptr, *Path);
        if (!TestNotNull(Path, Enum))
        {
            return false;
        }
        for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
        {
            AddInfo(FString::Printf(TEXT("enum=%s name=%s value=%lld display=%s"), Name,
                *Enum->GetNameStringByIndex(Index), Enum->GetValueByIndex(Index), *Enum->GetDisplayNameTextByIndex(Index).ToString()));
        }
        FNiagaraParameterStore Store;
        const FNiagaraVariable Variable(FNiagaraTypeDefinition(Enum), TEXT("User.Mode"));
        FString Error;
        if (!TestTrue(Path + TEXT(" accepts its exported enum name"),
            SetParameterValueText(Store, Variable, TEXT("NewEnumerator1"), true, &Error)))
        {
            AddInfo(Error);
            continue;
        }
        const int32 Expected = static_cast<int32>(Enum->GetValueByNameString(TEXT("NewEnumerator1")));
        TestEqual(TEXT("typed storage holds the enumerator value"), Store.GetParameterValue<int32>(Variable), Expected);
        TestEqual(TEXT("enum text is round-trip stable"), ParameterValueText(Store, Variable, &Error), FString(TEXT("NewEnumerator1")));
        TestFalse(TEXT("unknown enum name rejected"), SetParameterValueText(Store, Variable, TEXT("UnknownEnumerator"), false, &Error));
        TestFalse(TEXT("undefined numeric enum rejected"), SetParameterValueText(Store, Variable, TEXT("2147483647"), false, &Error));
        TestEqual(TEXT("rejection preserves typed storage"), Store.GetParameterValue<int32>(Variable), Expected);
        TestTrue(TEXT("defined numeric value accepted"), SetParameterValueText(Store, Variable, FString::FromInt(Expected), false, &Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraEmitterEnabled,
    "Nexus.Niagara.Transcode.EmitterEnabledRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraEmitterEnabled::RunTest(const FString& Parameters)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    if (!TestNotNull(TEXT("native template"), Template))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    auto& Emitter = System->GetEmitterHandles()[0];
    FString Error;
    for (const bool Enabled : { true, true, false, false, true })
    {
        TestTrue(TEXT("assignment succeeds including unchanged state"),
            ApplyEmitterProp(System, 0, TEXT("Enabled"), Enabled ? TEXT("true") : TEXT("false"), Error));
        TestEqual(TEXT("assignment readback"), Emitter.GetIsEnabled(), Enabled);
    }
    TestFalse(TEXT("malformed boolean is rejected"), ApplyEmitterProp(System, 0, TEXT("Enabled"), TEXT("typo"), Error));
    TestTrue(TEXT("invalid assignment leaves enabled state intact"), Emitter.GetIsEnabled());
    TestFalse(TEXT("invalid assignment explains the value error"), Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraModuleEnum,
    "Nexus.Niagara.Transcode.ModuleEnumRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraModuleEnum::RunTest(const FString& Parameters)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    auto* Initialize = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Spawn/Initialization/V2/InitializeParticle.InitializeParticle"));
    if (!TestNotNull(TEXT("native template"), Template) || !TestNotNull(TEXT("native initialize"), Initialize))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    auto& Emitter = System->GetEmitterHandles()[0];
    auto* Output = NiagaraModuleStack::ResolveOutputNode(&Emitter, TEXT("ParticleSpawnScript"));
    auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Initialize, *Output);
    FInputBindingContext Bindings;
    FInputBindingError Error;
    bool Changed = false;
    for (const TCHAR* Name : { TEXT("Lifetime Mode"), TEXT("Color Mode"), TEXT("Mass Mode"), TEXT("Position Mode"), TEXT("Sprite Size Mode") })
    {
        if (!TestTrue(FString(Name) + TEXT(" applies native enum input"), ApplyNativeInput(*System, Emitter, *Module,
            Name, TEXT("NewEnumerator1"), TEXT("ns_module_input_set"), false, Bindings, Changed, Error)))
        {
            AddInfo(Error.Message);
            continue;
        }
        TestTrue(TEXT("same enum write succeeds"), ApplyNativeInput(*System, Emitter, *Module,
            Name, TEXT("NewEnumerator1"), TEXT("ns_module_input_set"), false, Bindings, Changed, Error));
        TestFalse(TEXT("same enum write is unchanged"), Changed);
    }
    return true;
}
#endif
