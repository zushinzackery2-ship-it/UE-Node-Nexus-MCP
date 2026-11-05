#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputMetadata.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraDataInterfaceVectorField.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "UObject/StructOnScope.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraNativeModuleDefaults,
    "Nexus.Niagara.Transcode.NativeModuleDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraNativeModuleDefaults::RunTest(const FString& Parameters)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    if (!TestNotNull(TEXT("native emitter template"), Template))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    auto& Emitter = System->GetEmitterHandles()[0];
    auto* Output = NiagaraModuleStack::ResolveOutputNode(&Emitter, TEXT("ParticleUpdateScript"));
    struct FCase
    {
        const TCHAR* Path;
        const TCHAR* Input;
        double Expected;
    };
    const FCase Cases[] = {
        {TEXT("/Niagara/VectorFields/SampleVectorField.SampleVectorField"), TEXT("FalloffDistance"), 45.0},
        {TEXT("/Niagara/Modules/Update/Forces/WindForce.WindForce"), TEXT("Scale"), 0.5}
    };
    for (const auto& Case : Cases)
    {
        auto* Script = LoadObject<UNiagaraScript>(nullptr, Case.Path);
        if (!TestNotNull(Case.Path, Script))
        {
            return false;
        }
        auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output);
        const auto Pins = VfxTranscode::FindModuleInputDefaultPins(*Module->GetCalledGraph(), FName(FString(TEXT("Module.")) + Case.Input));
        if (!TestTrue(TEXT("native defining default pin exists"), Pins.Num() > 0))
        {
            return false;
        }
        TestEqual(TEXT("native graph defines the expected nonzero default"), FCString::Atod(*Pins[0]->DefaultValue), Case.Expected);
        FString Error;
        const auto Exported = VfxTranscode::ModuleJson(&Emitter, Output, Module, Error);
        if (!TestTrue(TEXT("native module export succeeds"), Exported.IsValid() && Error.IsEmpty()))
        {
            AddError(Error);
            return false;
        }
        const auto* Found = Exported->GetArrayField(TEXT("inputs")).FindByPredicate([&Case](const auto& Value)
        {
            return Value->AsObject()->GetStringField(TEXT("name")) == Case.Input;
        });
        if (!TestNotNull(TEXT("export contains the authored input"), Found))
        {
            return false;
        }
        const FString Default = (*Found)->AsObject()->GetStringField(TEXT("default"));
        AddInfo(FString::Printf(TEXT("module=%s input=%s pin_default=%s exported_default=%s"),
            Case.Path, Case.Input, *Pins[0]->DefaultValue, *Default));
        TestEqual(TEXT("exported default agrees with the native defining pin"), FCString::Atod(*Default), Case.Expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraClassTypeIdentity,
    "Nexus.Niagara.Transcode.ClassTypeIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraClassTypeIdentity::RunTest(const FString& Parameters)
{
    auto* Class = UNiagaraDataInterfaceVectorField::StaticClass();
    TestEqual(TEXT("class-based parameter types retain their reflected namespace"),
        VfxTranscode::FriendlyTypeName(FNiagaraTypeDefinition(Class)), Class->GetPathName());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraContextDefaults,
    "Nexus.Niagara.Transcode.ContextModuleDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraContextDefaults::RunTest(const FString& Parameters)
{
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr,
        TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    if (!TestNotNull(TEXT("native emitter template"), Template))
    {
        return false;
    }
    auto* System = NewObject<UNiagaraSystem>();
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    auto& Emitter = System->GetEmitterHandles()[0];
    struct FCase
    {
        const TCHAR* Script;
        const TCHAR* Group;
        const TCHAR* Input;
    };
    const FCase Cases[] = {
        {TEXT("/Niagara/Modules/Spawn/Location/BoxLocation.BoxLocation"), TEXT("ParticleSpawnScript"), TEXT("Rotate Box On Axis")},
        {TEXT("/Niagara/VectorFields/ApplyVectorField.ApplyVectorField"), TEXT("ParticleUpdateScript"), TEXT("Vector Field Vector")}
    };
    VfxTranscode::FInputBindingContext Context;
    for (const auto& Case : Cases)
    {
        auto* Output = NiagaraModuleStack::ResolveOutputNode(&Emitter, Case.Group);
        auto* Script = LoadObject<UNiagaraScript>(nullptr, Case.Script);
        auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output);
        VfxTranscode::FInputBindingError BindingError;
        FNiagaraVariable Variable;
        if (!VfxTranscode::ResolveModuleInput(*Module, Case.Input, Variable, BindingError))
        {
            AddError(BindingError.Message);
            return false;
        }
        auto* Input = Context.Resolve(*System, Emitter, *Module, Variable, BindingError);
        if (!TestNotNull(TEXT("native stack resolves the input"), Input))
        {
            AddError(BindingError.Message);
            return false;
        }
        FString Expected;
        if (Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local)
        {
            const auto Local = Input->GetLocalValueStruct();
            const auto* Value = reinterpret_cast<const FVector3f*>(Local->GetStructMemory());
            TestEqual(TEXT("box axis native default"), *Value, FVector3f(0, 0, 1));
            FNiagaraVariable NativeValue(Variable.GetType(), Variable.GetName());
            NativeValue.SetData(Local->GetStructMemory());
            GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(NativeValue, Expected);
        }
        else
        {
            Expected = FString::Printf(TEXT("@link(%s)"), *Input->GetLinkedValueHandle().GetParameterHandleString().ToString());
            TestEqual(TEXT("field native default binding"), Expected, FString(TEXT("@link(Output.SampleVectorField.SampledVector)")));
        }
        FString Error;
        const auto Exported = VfxTranscode::ModuleJson(&Emitter, Output, Module, Error);
        if (!TestNotNull(TEXT("context module export"), Exported.Get()))
        {
            AddError(Error);
            return false;
        }
        const auto* Found = Exported->GetArrayField(TEXT("inputs")).FindByPredicate([&Case](const auto& Value)
        {
            return Value->AsObject()->GetStringField(TEXT("name")) == Case.Input;
        });
        FString Default;
        const bool HasDefault = Found && (*Found)->AsObject()->TryGetStringField(TEXT("default"), Default);
        TestTrue(TEXT("resolved native default is exported"), HasDefault);
        AddInfo(FString::Printf(TEXT("input=%s native=%s exported=%s"), Case.Input, *Expected, *Default));
        if (HasDefault)
        {
            TestEqual(TEXT("export agrees with native stack"), Default, Expected);
        }
    }
    return true;
}
#endif
