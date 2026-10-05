#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NiagaraParameterStore.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "Niagara/Transcode/Values/Storage/NexusNiagaraStorage.h"
#include "NiagaraDataInterfaceArrayFloat.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraParameterAlignment,
    "Nexus.Niagara.ParameterAlignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraParameterAlignment::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    for (int32 Padding = 1; Padding <= 3; ++Padding)
    {
        FNiagaraParameterStore Store;
        for (int32 Index = 0; Index < Padding; ++Index)
        {
            const FNiagaraVariable Scalar(FNiagaraTypeDefinition::GetFloatDef(),
                FName(*FString::Printf(TEXT("User.Padding%d"), Index)));
            TestTrue(TEXT("scalar declaration"), SetParameterValueText(Store, Scalar, TEXT("1"), true));
        }
        const FNiagaraVariable Quaternion(FNiagaraTypeDefinition::GetQuatDef(), TEXT("User.Rotation"));
        const FNiagaraVariable Vector(FNiagaraTypeDefinition::GetVec4Def(), TEXT("User.Vector"));
        TestTrue(TEXT("quaternion declaration"), SetParameterValueText(Store, Quaternion, TEXT("(X=0,Y=0,Z=0,W=1)"), true));
        TestTrue(TEXT("vector declaration"), SetParameterValueText(Store, Vector, TEXT("(X=1,Y=2,Z=3,W=4)"), true));
        TestEqual(TEXT("packed quaternion offset"), Store.IndexOf(Quaternion), Padding * 4);
        TestEqual(TEXT("quaternion round trip"), ParameterValueText(Store, Quaternion), FString(TEXT("(X=0,Y=0,Z=0,W=1)")));
        TestEqual(TEXT("vector round trip"), ParameterValueText(Store, Vector), FString(TEXT("(X=1,Y=2,Z=3,W=4)")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraStorageRejection,
    "Nexus.Niagara.StorageRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraStorageRejection::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    FNiagaraParameterStore Store;
    const FNiagaraVariable Scalar(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Scalar"));
    TestTrue(TEXT("scalar declaration"), SetParameterValueText(Store, Scalar, TEXT("2.5"), true));
    double WrongSize = 0;
    FString Error;
    TestFalse(TEXT("typed size rejected before copy"), Storage::ReadBytes(Store, Scalar, &WrongSize, sizeof(WrongSize), Error));
    TestFalse(TEXT("size diagnostic retained"), Error.IsEmpty());
    const FNiagaraVariable Missing(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Missing"));
    float Value = 123;
    TestFalse(TEXT("missing parameter refused"), Storage::ReadBytes(Store, Missing, &Value, sizeof(Value), Error));
    TestEqual(TEXT("failed copy leaves output unchanged"), Value, 123.0f);
    const_cast<TArray<uint8>&>(Store.GetParameterDataArray()).Reset();
    AddExpectedError(TEXT("Niagara value read rejected"), EAutomationExpectedErrorFlags::Contains, 1);
    TestTrue(TEXT("bad range has no exported value"), ParameterValueText(Store, Scalar, &Error).IsEmpty());
    TestTrue(TEXT("range diagnostic"), Error.Contains(TEXT("range exceeds storage")));
    TestFalse(TEXT("bad range also refuses writes"), SetParameterValueText(Store, Scalar, TEXT("3"), false, &Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraObjectStorageRejection,
    "Nexus.Niagara.ObjectStorageRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraObjectStorageRejection::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    FNiagaraParameterStore Store;
    const FNiagaraVariable Interface(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()), TEXT("User.Interface"));
    TestTrue(TEXT("null interface declaration"), SetParameterValueText(Store, Interface, TEXT("None"), true));
    FString Error;
    TestEqual(TEXT("valid null is explicit None"), ParameterValueText(Store, Interface, &Error), FString(TEXT("None")));
    TestTrue(TEXT("valid null has no error"), Error.IsEmpty());
    const_cast<TArray<UNiagaraDataInterface*>&>(Store.GetDataInterfaces()).Reset();
    AddExpectedError(TEXT("Niagara value read rejected"), EAutomationExpectedErrorFlags::Contains, 1);
    TestTrue(TEXT("invalid object slot has no value"), ParameterValueText(Store, Interface, &Error).IsEmpty());
    TestTrue(TEXT("slot diagnostic"), Error.Contains(TEXT("object storage index")));
    TestFalse(TEXT("invalid slot refuses writes"), SetParameterValueText(Store, Interface, TEXT("None"), false, &Error));
    return true;
}

#endif
