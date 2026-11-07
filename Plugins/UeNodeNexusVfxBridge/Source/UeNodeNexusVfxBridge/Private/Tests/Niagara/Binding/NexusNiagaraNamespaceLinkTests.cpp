#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Niagara/Fixtures/NexusNiagaraVectorFieldFixture.h"
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
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace
{
using namespace UeNodeNexusBridge;
using namespace VfxTranscode;

using Tests::FNiagaraVectorFieldFixture;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraNamespaceRoundTrip,
    "Nexus.Niagara.NamespaceLink.ParticleAndPrecedingOutputRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraNamespaceRoundTrip::RunTest(const FString& Parameters)
{
    FNiagaraVectorFieldFixture Fixture;
    if (!Fixture.Initialize(*this, true))
    {
        return false;
    }
    FInputBindingError Error;
    bool bChanged = false;
    const FGuid SampleGuid = Fixture.Sample->NodeGuid;
    FNiagaraVariable Position;
    if (!TestTrue(TEXT("position input resolves"), ResolveModuleInput(*Fixture.Sample, TEXT("SamplePoint"), Position, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    auto* Input = Fixture.Bindings.Resolve(*Fixture.System, *Fixture.Emitter, *Fixture.Sample, Position, Error);
    if (!TestNotNull(TEXT("native position input"), Input))
    {
        AddError(Error.Message);
        return false;
    }
    const auto BeforeMode = Input->GetValueMode();
    const auto BeforeHandle = Input->GetLinkedValueHandle();
    TestTrue(TEXT("particle-position dry-run validates the native conversion"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), TEXT("Particles.Position"),
        TEXT("ns_module_input_convert"), true, Fixture.Bindings, bChanged, Error));
    TestFalse(TEXT("dry-run never writes"), bChanged);
    TestTrue(TEXT("dry-run preserves value mode"), Input->GetValueMode() == BeforeMode);
    TestTrue(TEXT("dry-run preserves existing handle"), Input->GetLinkedValueHandle() == BeforeHandle);
    if (!TestTrue(TEXT("particle position converts through the native setter"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), TEXT("Particles.Position"),
        TEXT("ns_module_input_convert"), false, Fixture.Bindings, bChanged, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    Input = Fixture.Bindings.Resolve(*Fixture.System, *Fixture.Emitter, *Fixture.Sample, Position, Error);
    FNiagaraVariable ConversionSource;
    UNiagaraScript* ConversionScript = nullptr;
    TestTrue(TEXT("actual conversion remains available"), Input && ResolveNativeConversion(
        *Input, TEXT("Particles.Position"), ConversionSource, ConversionScript, Error));
    TestTrue(TEXT("native conversion and nested position-link readback"), Input
        && MatchesNativeConversion(*Input, ConversionSource, ConversionScript));
    TestTrue(TEXT("same native conversion is repeatable"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), TEXT("Particles.Position"),
        TEXT("ns_module_input_convert"), false, Fixture.Bindings, bChanged, Error));
    TestFalse(TEXT("repeat does not recreate converter"), bChanged);
    FString ExportError;
    const auto Exported = ModuleJson(Fixture.Emitter, Fixture.Output, Fixture.Sample, ExportError);
    if (!TestTrue(TEXT("native conversion export succeeds"), Exported.IsValid() && ExportError.IsEmpty()))
    {
        AddError(ExportError);
        return false;
    }
    bool bConverted = false;
    for (const auto& Value : Exported->GetArrayField(TEXT("inputs")))
    {
        const auto Item = Value->AsObject();
        FString Handle;
        if (Item->GetStringField(TEXT("name")) == TEXT("SamplePoint"))
        {
            bConverted = Item->TryGetStringField(TEXT("converted"), Handle) && Handle == TEXT("Particles.Position");
        }
    }
    TestTrue(TEXT("mirror distinguishes native cast from opaque dynamic input"), bConverted);
    TestTrue(TEXT("module identity preserved"), Fixture.Sample->NodeGuid == SampleGuid);
    const FString Output = Fixture.OutputHandle();
    if (!TestTrue(TEXT("preceding vector-field output binds"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Force, TEXT("Vector Field Vector"), Output,
        TEXT("ns_module_input_link"), false, Fixture.Bindings, bChanged, Error)))
    {
        AddError(Error.Message);
        return false;
    }
    FNiagaraVariable Vector;
    TestTrue(TEXT("force input resolves"), ResolveModuleInput(*Fixture.Force, TEXT("Vector Field Vector"), Vector, Error));
    Input = Fixture.Bindings.Resolve(*Fixture.System, *Fixture.Emitter, *Fixture.Force, Vector, Error);
    TestTrue(TEXT("output native readback"), Input
        && Input->GetValueMode() == UNiagaraStackFunctionInput::EValueMode::Linked
        && Input->GetLinkedValueHandle().GetParameterHandleString() == FName(*Output));
    TestTrue(TEXT("same output binding is repeatable"), ApplyNativeInput(
        *Fixture.System, *Fixture.Emitter, *Fixture.Force, TEXT("Vector Field Vector"), Output,
        TEXT("ns_module_input_link"), false, Fixture.Bindings, bChanged, Error));
    TestFalse(TEXT("repeat does not recreate overrides"), bChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraNamespaceRejections,
    "Nexus.Niagara.NamespaceLink.RejectMissingTypeAndForwardWrites",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraNamespaceRejections::RunTest(const FString& Parameters)
{
    FNiagaraVectorFieldFixture Fixture;
    if (!Fixture.Initialize(*this, true))
    {
        return false;
    }
    AddExpectedError(TEXT("linked_parameter_unavailable"), EAutomationExpectedErrorFlags::Contains, 8);
    AddExpectedError(TEXT("invalid_parameter_link"), EAutomationExpectedErrorFlags::Contains, 2);
    AddExpectedError(TEXT("native_conversion_unavailable"), EAutomationExpectedErrorFlags::Contains, 2);
    const TArray<TPair<FString, FString>> Invalid = {
        {TEXT("Particles.Missing"), TEXT("linked_parameter_unavailable")},
        {TEXT("Engine.DeltaTime"), TEXT("linked_parameter_unavailable")},
        {TEXT("Particles.Position"), TEXT("linked_parameter_unavailable")},
        {TEXT("Particles..Position"), TEXT("invalid_parameter_link")}
    };
    const FGuid SampleGuid = Fixture.Sample->NodeGuid;
    FInputBindingError Error;
    bool bChanged = false;
    for (const auto& Case : Invalid)
    {
        for (const bool bDryRun : {true, false})
        {
            TestFalse(TEXT("unavailable or malformed input is rejected"), ApplyNativeInput(
                *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), Case.Key,
                TEXT("ns_module_input_link"), bDryRun, Fixture.Bindings, bChanged, Error));
            TestEqual(TEXT("specific rejection retained"), Error.Code, Case.Value);
            TestFalse(TEXT("rejection preserves input"), bChanged);
        }
    }
    TestTrue(TEXT("rejection preserves module identity"), Fixture.Sample->NodeGuid == SampleGuid);
    TestEqual(TEXT("no rejected request claims a result"), Fixture.Bindings.Results.Num(), 0);
    for (const bool bDryRun : {true, false})
    {
        TestFalse(TEXT("conversion cannot create a missing source"), ApplyNativeInput(
            *Fixture.System, *Fixture.Emitter, *Fixture.Sample, TEXT("SamplePoint"), TEXT("Particles.Missing"),
            TEXT("ns_module_input_convert"), bDryRun, Fixture.Bindings, bChanged, Error));
        TestEqual(TEXT("missing conversion source rejected"), Error.Code, FString(TEXT("native_conversion_unavailable")));
    }
    FNiagaraVectorFieldFixture Forward;
    if (!Forward.Initialize(*this, false))
    {
        return false;
    }
    for (const bool bDryRun : {true, false})
    {
        TestFalse(TEXT("later module output is not available before its write"), ApplyNativeInput(
            *Forward.System, *Forward.Emitter, *Forward.Force, TEXT("Vector Field Vector"), Forward.OutputHandle(),
            TEXT("ns_module_input_link"), bDryRun, Forward.Bindings, bChanged, Error));
        TestEqual(TEXT("forward write rejection"), Error.Code, FString(TEXT("linked_parameter_unavailable")));
        TestTrue(TEXT("diagnostic carries actual target type"), Error.Message.Contains(TEXT("expected_type=")));
        TestTrue(TEXT("diagnostic carries actual alternatives"), Error.Message.Contains(TEXT("available_handles=")));
    }
    return true;
}
#endif
