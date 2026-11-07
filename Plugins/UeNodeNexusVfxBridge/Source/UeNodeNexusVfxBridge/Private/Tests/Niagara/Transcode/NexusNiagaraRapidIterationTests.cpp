#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Modules/Binding/NexusNiagaraInputMetadata.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UObject/StructOnScope.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraRapidIterationDefaults,
    "Nexus.Niagara.Transcode.RapidIterationDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraRapidIterationDefaults::RunTest(const FString& Parameters)
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
        const TCHAR* Script;
        const TCHAR* Name;
        const TCHAR* Link;
        float RapidValue;
        bool bEmptyDefault = false;
    };
    const FCase Cases[] = {
        {TEXT("/Niagara/Modules/Update/Forces/AerodynamicDrag.AerodynamicDrag"), TEXT("Mass"), TEXT("Particles.Mass"), 0.0f},
        {TEXT("/Niagara/Modules/Collision/Collision.Collision"), TEXT("Particle Mass"), TEXT("Particles.Mass"), 0.0f},
        {TEXT("/Niagara/Modules/Update/Forces/WindForce.WindForce"), TEXT("Scale"), TEXT(""), 3.25f},
        {TEXT("/Niagara/Modules/Update/Forces/WindForce.WindForce"), TEXT("Scale"), TEXT(""), 250.0f, true}
    };
    for (const auto& Case : Cases)
    {
        auto* ModuleScript = LoadObject<UNiagaraScript>(nullptr, Case.Script);
        if (!TestNotNull(Case.Script, ModuleScript))
        {
            return false;
        }
        UNiagaraScript* OriginalScript = ModuleScript;
        if (Case.bEmptyDefault)
        {
            // Keep the stock script intact while reproducing an authored empty default.
            ModuleScript = DuplicateObject<UNiagaraScript>(ModuleScript, GetTransientPackage());
        }
        auto* Module = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModuleScript, *Output);
        FNiagaraVariable Variable;
        FNiagaraVariable RapidVariable;
        UNiagaraScript* Script = nullptr;
        VfxTranscode::FInputBindingError BindingError;
        const bool Resolved = VfxTranscode::ResolveModuleInput(*Module, Case.Name, Variable, BindingError)
            && VfxTranscode::ResolveRapidIterationInput(Emitter, Output->GetUsage(), Output->GetUsageId(),
                Module, Case.Name, RapidVariable, Script);
        if (!TestTrue(TEXT("resolve input and its rapid-iteration storage"), Resolved))
        {
            AddError(BindingError.Message);
            return false;
        }
        if (Case.bEmptyDefault)
        {
            const auto* OriginalSource = Cast<UNiagaraScriptSource>(OriginalScript->GetLatestSource());
            if (!TestTrue(TEXT("empty default fixture owns a cloned graph"), OriginalSource
                && OriginalSource->NodeGraph && Module->GetCalledGraph() != OriginalSource->NodeGraph))
            {
                return false;
            }
            const auto OriginalPins = VfxTranscode::FindModuleInputDefaultPins(*OriginalSource->NodeGraph, Variable.GetName());
            if (!TestTrue(TEXT("stock script has the defining default"), !OriginalPins.IsEmpty()))
            {
                return false;
            }
            TArray<FString> OriginalDefaults;
            for (const UEdGraphPin* Pin : OriginalPins)
            {
                OriginalDefaults.Add(Pin->DefaultValue);
            }
            const auto Pins = VfxTranscode::FindModuleInputDefaultPins(*Module->GetCalledGraph(), Variable.GetName());
            if (!TestTrue(TEXT("empty default fixture has a defining pin"), !Pins.IsEmpty()))
            {
                return false;
            }
            for (const UEdGraphPin* Pin : Pins)
            {
                const_cast<UEdGraphPin*>(Pin)->DefaultValue.Empty();
            }
            Module->GetCalledGraph()->NotifyGraphChanged();
            for (int32 Index = 0; Index < OriginalPins.Num(); ++Index)
            {
                TestEqual(TEXT("stock script default remains"), OriginalPins[Index]->DefaultValue, OriginalDefaults[Index]);
            }
            const FString Default = VfxTranscode::ModuleInputDefaultText(Variable,
                Module->GetCalledGraph()->GetScriptVariable(Variable.GetName()), Module->GetCalledGraph());
            if (!TestTrue(TEXT("empty defining default exports a typed value"), !Default.IsEmpty()))
            {
                return false;
            }
            TestEqual(TEXT("native empty float default is zero"), FCString::Atof(*Default), 0.0f);
            TestTrue(TEXT("empty override still has no readable value"), VfxTranscode::ModuleInputPinValueText(*Pins[0]).IsEmpty());
        }
        else
        {
            Script->RapidIterationParameters.SetParameterValue(Case.RapidValue, RapidVariable, true);
        }
        VfxTranscode::FInputBindingContext Context;
        auto* Input = Context.Resolve(*System, Emitter, *Module, Variable, BindingError);
        if (!TestNotNull(TEXT("native stack input"), Input))
        {
            AddError(BindingError.Message);
            return false;
        }
        if (Case.bEmptyDefault)
        {
            const auto DefaultValue = Input->GetLocalValueStruct();
            if (!TestTrue(TEXT("native stack allocates the empty local default"), DefaultValue.IsValid()
                && Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Local))
            {
                return false;
            }
            TestEqual(TEXT("native stack reads the defining type default"),
                *reinterpret_cast<const float*>(DefaultValue->GetStructMemory()), 0.0f);
            bool bChanged = false;
            if (!TestTrue(TEXT("native setter writes the empty-default input"),
                VfxTranscode::ApplyNativeInput(*System, Emitter, *Module, Case.Name, FString::SanitizeFloat(Case.RapidValue),
                    TEXT("ns_module_input_set"), false, Context, bChanged, BindingError)))
            {
                AddError(BindingError.Message);
                return false;
            }
            TestTrue(TEXT("native empty-default input changed"), bChanged);
            const uint8* Bytes = Script->RapidIterationParameters.GetParameterData(RapidVariable);
            TestTrue(TEXT("native setter allocates the requested rapid bytes"), Bytes
                && *reinterpret_cast<const float*>(Bytes) == Case.RapidValue);
        }
        const bool Linked = FString(Case.Link).Len() > 0;
        FString Native;
        if (Linked)
        {
            TestEqual(TEXT("native stack ignores stale rapid storage for a linked default"),
                Input->GetValueMode(), UNiagaraStackFunctionInput::EValueMode::Linked);
            Native = Input->GetLinkedValueHandle().GetParameterHandleString().ToString();
            TestEqual(TEXT("native particle attribute"), Native, FString(Case.Link));
            TestFalse(TEXT("default particle binding needs no reset"), Input->CanReset());
        }
        else
        {
            TestEqual(TEXT("native local default uses rapid storage"),
                Input->GetValueMode(), UNiagaraStackFunctionInput::EValueMode::Local);
            const auto Value = Input->GetLocalValueStruct();
            if (!TestTrue(TEXT("local value is allocated"), Value.IsValid()))
            {
                return false;
            }
            const float Actual = *reinterpret_cast<const float*>(Value->GetStructMemory());
            TestEqual(TEXT("native local rapid value"), Actual, Case.RapidValue);
            Native = FString::SanitizeFloat(Actual);
        }
        FString Error;
        const auto Exported = VfxTranscode::ModuleJson(&Emitter, Output, Module, Error);
        if (!TestTrue(TEXT("module export succeeds"), Exported.IsValid() && Error.IsEmpty()))
        {
            AddError(Error);
            return false;
        }
        const auto* Found = Exported->GetArrayField(TEXT("inputs")).FindByPredicate([&Case](const auto& Value)
        {
            return Value->AsObject()->GetStringField(TEXT("name")) == Case.Name;
        });
        if (!TestNotNull(TEXT("exported input"), Found))
        {
            return false;
        }
        const auto Row = (*Found)->AsObject();
        AddInfo(FString::Printf(TEXT("input=%s empty_default=%d native=%s rapid=%g exported=%s override=%d"), Case.Name,
            Case.bEmptyDefault, *Native, Case.RapidValue, *Row->GetStringField(TEXT("value")), Row->GetBoolField(TEXT("has_override"))));
        TestEqual(TEXT("exported override eligibility agrees with native stack"), Row->GetBoolField(TEXT("has_override")), !Linked);
        if (Linked)
        {
            TestEqual(TEXT("exported value retains the default link"), Row->GetStringField(TEXT("value")),
                FString::Printf(TEXT("@link(%s)"), Case.Link));
        }
        else
        {
            TestTrue(TEXT("exported local value is present"), !Row->GetStringField(TEXT("value")).IsEmpty());
            TestEqual(TEXT("exported local override is retained"), FCString::Atof(*Row->GetStringField(TEXT("value"))), Case.RapidValue);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraBooleanPinValues,
    "Nexus.Niagara.Transcode.BooleanPinValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraBooleanPinValues::RunTest(const FString& Parameters)
{
    auto* Node = NewObject<UNiagaraNodeFunctionCall>();
    const auto* Schema = GetDefault<UEdGraphSchema_Niagara>();
    for (const auto& Type : {FNiagaraTypeDefinition::GetBoolDef(), FNiagaraTypeDefinition::GetBoolDef().ToStaticDef()})
    {
        const FNiagaraVariable Variable(Type, TEXT("Constants.Test.Boolean"));
        FNiagaraParameterStore Store;
        Store.SetParameterValue(FNiagaraBool(false), Variable, true);
        TArray<FNiagaraVariable> Before;
        Store.GetParameters(Before);
        if (!TestEqual(TEXT("native store has one boolean"), Before.Num(), 1))
        {
            return false;
        }
        TestEqual(TEXT("native store retains the supplied static flag"), Before[0].GetType().IsStatic(), Type.IsStatic());
        FString Error;
        TestEqual(TEXT("stored false retains its scalar meaning"), VfxTranscode::ParameterValueText(Store, Variable, &Error), FString(TEXT("False")));
        TestTrue(TEXT("boolean storage read succeeds"), Error.IsEmpty());
        TestTrue(TEXT("typed boolean text write succeeds"), VfxTranscode::SetParameterValueText(Store, Variable, TEXT("true"), false, &Error));
        TestTrue(TEXT("native boolean store contains true"), Store.GetParameterValue<FNiagaraBool>(Variable).GetValue());
        TestEqual(TEXT("stored true retains its scalar meaning"), VfxTranscode::ParameterValueText(Store, Variable, &Error), FString(TEXT("True")));
        TArray<FNiagaraVariable> After;
        Store.GetParameters(After);
        if (!TestEqual(TEXT("text conversion retains the parameter count"), After.Num(), Before.Num()))
        {
            return false;
        }
        // IndexOf compares type names, which intentionally alias static and
        // non-static definitions. Inspect the stored definition itself.
        TestTrue(TEXT("text conversion preserves the stored variable identity"), After[0] == Before[0]);
        AddInfo(FString::Printf(TEXT("storage static_before=%d static_after=%d count=%d value=%d"),
            Before[0].GetType().IsStatic(), After[0].GetType().IsStatic(), After.Num(),
            Store.GetParameterValue<FNiagaraBool>(Variable).GetValue()));
        auto* Pin = Node->CreatePin(EGPD_Input, UEdGraphSchema_Niagara::TypeDefinitionToPinType(Type), TEXT("Boolean"));
        for (const auto* Text : {TEXT("()"), TEXT("false"), TEXT("true")})
        {
            Pin->DefaultValue = Text;
            const FNiagaraVariable Value = Schema->PinToNiagaraVariable(Pin);
            if (!TestTrue(TEXT("native boolean storage is allocated"), Value.IsDataAllocated()))
            {
                return false;
            }
            const bool Expected = FString(Text) == TEXT("true");
            const bool Actual = Value.GetValue<FNiagaraBool>().GetValue();
            const FString Exported = VfxTranscode::ModuleInputPinValueText(*Pin);
            AddInfo(FString::Printf(TEXT("type=%s static=%d raw=%s native=%d exported=%s"),
                *Type.GetName(), Type.IsStatic(), Text, Actual, *Exported));
            TestEqual(TEXT("native boolean interpretation"), Actual, Expected);
            TestEqual(TEXT("typed pin export retains boolean semantics"), Exported, FString(Expected ? TEXT("true") : TEXT("false")));
        }
    }
    return true;
}
#endif
