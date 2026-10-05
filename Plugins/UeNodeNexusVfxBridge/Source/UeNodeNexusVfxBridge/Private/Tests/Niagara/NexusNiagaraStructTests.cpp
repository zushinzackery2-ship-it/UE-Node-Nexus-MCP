#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NiagaraParameterStore.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "Niagara/Transcode/Values/Storage/NexusNiagaraStorage.h"
#include "UObject/StructOnScope.h"
#include "NexusNiagaraLwcFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraNumericStructure,
    "Nexus.Niagara.NumericStructure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraNumericStructure::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    FNiagaraParameterStore Store;
    const FNiagaraVariable Scalar(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Padding"));
    TestTrue(TEXT("packed prefix"), SetParameterValueText(Store, Scalar, TEXT("1"), true));
    const FNiagaraVariable Matrix(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("User.Matrix"));
    const FMatrix44f Expected = FMatrix44f::Identity;
    TestTrue(TEXT("matrix declaration"), Store.SetParameterValue(Expected, Matrix, true));
    TestEqual(TEXT("matrix is misaligned"), Store.IndexOf(Matrix), 4);
    FString Error;
    const FString Text = ParameterValueText(Store, Matrix, &Error);
    TestTrue(TEXT("matrix read succeeds"), Error.IsEmpty() && !Text.IsEmpty());
    FStructOnScope Actual(Matrix.GetType().GetScriptStruct());
    TestNotNull(TEXT("matrix text imports"), Matrix.GetType().GetScriptStruct()->ImportText(
        *Text, Actual.GetStructMemory(), nullptr, PPF_None, GWarn, TEXT("Matrix")));
    TestTrue(TEXT("all matrix fields retained"), FMemory::Memcmp(&Expected, Actual.GetStructMemory(), sizeof(Expected)) == 0);
    const FNiagaraVariable Enum(FNiagaraTypeDefinition(FNiagaraTypeDefinition::GetCoordinateSpaceEnum()), TEXT("User.Space"));
    TestTrue(TEXT("enum declaration"), Store.SetParameterValue<int32>(1, Enum, true));
    TestFalse(TEXT("enum numeric text exported"), ParameterValueText(Store, Enum, &Error).IsEmpty());
    TestTrue(TEXT("enum read diagnostic clear"), Error.IsEmpty());
    FNiagaraParameterStore LwcStore;
    const FNiagaraTypeDefinition LwcType(FNexusNiagaraLwcFixture::StaticStruct());
    const FNiagaraTypeDefinition Simulation = FNiagaraTypeHelper::GetSWCType(LwcType);
    FNiagaraVariable Lwc(LwcType, TEXT("User.DoubleStruct"));
    const FNexusNiagaraLwcFixture ExpectedLwc;
    Lwc.SetValue(ExpectedLwc);
    TestTrue(TEXT("LWC declaration"), LwcStore.AddParameter(Lwc, false));
    TestTrue(TEXT("engine uses simulation converter"), LwcStore.GetStructConverter(Lwc).IsValid());
    // Execution stores may allocate only simulation bytes rather than LWC bytes.
    const_cast<TArray<uint8>&>(LwcStore.GetParameterDataArray()).SetNum(Simulation.GetSize());
    FNexusNiagaraLwcFixture ActualLwc;
    TestTrue(TEXT("simulation-sized source permits LWC destination"),
        Storage::ReadBytes(LwcStore, Lwc, &ActualLwc, sizeof(ActualLwc), Error));
    TestEqual(TEXT("LWC double field"), ActualLwc.Scalar, ExpectedLwc.Scalar);
    TestEqual(TEXT("LWC following integer field"), ActualLwc.Flag, ExpectedLwc.Flag);
    TestFalse(TEXT("LWC numeric text exported"), ParameterValueText(LwcStore, Lwc, &Error).IsEmpty());
    TestTrue(TEXT("LWC text diagnostic clear"), Error.IsEmpty());
    return true;
}

#endif
