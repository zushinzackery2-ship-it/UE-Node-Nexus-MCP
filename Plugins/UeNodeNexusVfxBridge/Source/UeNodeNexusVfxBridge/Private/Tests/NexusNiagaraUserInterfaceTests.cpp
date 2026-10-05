#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/Material.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "UObject/UObjectHash.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"

using namespace UeNodeNexusBridge::VfxTranscode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraUserInterface,
    "Nexus.Niagara.UserInterfaceValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraUserInterface::RunTest(const FString& Parameters)
{
    auto* System = NewObject<UNiagaraSystem>();
    FNiagaraParameterStore& Store = System->GetExposedParameters();
    const FNiagaraVariable Integer(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayInt32::StaticClass()), TEXT("User.Integers"));
    const FNiagaraVariable Floating(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()), TEXT("User.Floats"));
    const FNiagaraVariable Vector(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat3::StaticClass()), TEXT("User.Vectors"));
    const FNiagaraVariable Material(FNiagaraTypeDefinition(UMaterial::StaticClass()), TEXT("User.Material"));

    TArray<UObject*> ObjectsBefore;
    GetObjectsWithOuter(System, ObjectsBefore, false);
    TestTrue(TEXT("declare empty int array without accessing UObject storage"), SetParameterValueText(Store, Integer, TEXT("None"), true));
    TestTrue(TEXT("declare empty float array"), SetParameterValueText(Store, Floating, TEXT("None"), true));
    TestTrue(TEXT("declare empty vector array"), SetParameterValueText(Store, Vector, TEXT("None"), true));
    TestEqual(TEXT("empty interface round trip"), ParameterValueText(Store, Integer), FString(TEXT("None")));
    TestNull(TEXT("explicit None preserves null interface"), Store.GetDataInterface(Integer));
    TArray<UObject*> ObjectsAfter;
    GetObjectsWithOuter(System, ObjectsAfter, false);
    TestEqual(TEXT("explicit None does not allocate discarded interfaces"), ObjectsAfter.Num(), ObjectsBefore.Num());

    auto* Integers = NewObject<UNiagaraDataInterfaceArrayInt32>();
    Integers->IntData.Add(17);
    auto* Floats = NewObject<UNiagaraDataInterfaceArrayFloat>();
    Floats->FloatData.Add(0.875f);
    auto* Vectors = NewObject<UNiagaraDataInterfaceArrayFloat3>();
    Vectors->FloatData.Add(FVector(-1, 1, 0));
    Vectors->InternalFloatData.Add(FVector3f(-1, 1, 0));
    TestTrue(TEXT("set explicit int interface"), SetParameterValueText(Store, Integer, Integers->GetPathName(), false));
    TestTrue(TEXT("set explicit float interface"), SetParameterValueText(Store, Floating, Floats->GetPathName(), false));
    TestTrue(TEXT("set explicit vector interface"), SetParameterValueText(Store, Vector, Vectors->GetPathName(), false));
    TestEqual(TEXT("int interface pointer"), Store.GetDataInterface(Integer), static_cast<UNiagaraDataInterface*>(Integers));
    TestEqual(TEXT("float interface pointer"), Store.GetDataInterface(Floating), static_cast<UNiagaraDataInterface*>(Floats));
    TestEqual(TEXT("vector interface pointer"), Store.GetDataInterface(Vector), static_cast<UNiagaraDataInterface*>(Vectors));
    TestEqual(TEXT("int interface path round trip"), ParameterValueText(Store, Integer), Integers->GetPathName());
    TestEqual(TEXT("float interface path round trip"), ParameterValueText(Store, Floating), Floats->GetPathName());
    TestEqual(TEXT("vector interface path round trip"), ParameterValueText(Store, Vector), Vectors->GetPathName());

    TestFalse(TEXT("wrong interface type rejected"), SetParameterValueText(Store, Integer, Floats->GetPathName(), false));
    TestEqual(TEXT("rejection preserves int interface"), Store.GetDataInterface(Integer), static_cast<UNiagaraDataInterface*>(Integers));
    TestTrue(TEXT("ordinary UObject still supported"), SetParameterValueText(Store, Material, TEXT("None"), true));
    TestEqual(TEXT("ordinary UObject round trip"), ParameterValueText(Store, Material), FString(TEXT("None")));
    auto* MaterialObject = NewObject<UMaterial>();
    TestTrue(TEXT("set ordinary UObject beside interfaces"), SetParameterValueText(Store, Material, MaterialObject->GetPathName(), false));
    TestEqual(TEXT("ordinary UObject pointer"), Store.GetUObject(Material), static_cast<UObject*>(MaterialObject));
    TestTrue(TEXT("clear interface beside ordinary UObject"), SetParameterValueText(Store, Integer, TEXT("None"), true));
    TestNull(TEXT("cleared interface"), Store.GetDataInterface(Integer));
    TestEqual(TEXT("clearing interface preserves UObject"), Store.GetUObject(Material), static_cast<UObject*>(MaterialObject));

    FNiagaraParameterStore UnownedStore;
    TestFalse(TEXT("missing parameter is not implicitly added"), SetParameterValueText(UnownedStore, Integer, TEXT("None"), false));
    TestTrue(TEXT("unowned store declares null without constructing an object"), SetParameterValueText(UnownedStore, Integer, TEXT("None"), true));
    TestTrue(TEXT("unowned store accepts explicit interface"), SetParameterValueText(UnownedStore, Floating, Floats->GetPathName(), true));
    TestEqual(TEXT("unowned interface round trip"), ParameterValueText(UnownedStore, Floating), Floats->GetPathName());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraUserInterfaceApply,
    "Nexus.Niagara.UserInterfaceApply",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraUserInterfaceApply::RunTest(const FString& Parameters)
{
    // Preserve the captured request's short type names; UE warns on each resolution.
    AddExpectedError(TEXT("provided for TryFindType. Please convert it to a path name"), EAutomationExpectedErrorFlags::Contains, 6);
    auto* System = NewObject<UNiagaraSystem>();
    const TCHAR* Names[] = { TEXT("ProbeIntegers"), TEXT("ProbeFloats"), TEXT("ProbeVectors") };
    const TCHAR* Types[] = { TEXT("NiagaraDataInterfaceArrayInt32"), TEXT("NiagaraDataInterfaceArrayFloat"), TEXT("NiagaraDataInterfaceArrayFloat3") };
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto Op = MakeShared<FJsonObject>();
        Op->SetStringField(TEXT("name"), Names[Index]);
        Op->SetStringField(TEXT("type"), Types[Index]);
        Op->SetStringField(TEXT("value"), TEXT("None"));
        FString Error;
        TestTrue(TEXT("recorded user parameter addition"), ApplyUserParam(System, TEXT("ns_user_param_add"), Op, Error));
        TestTrue(TEXT("repeat addition preserves parameter identity"), ApplyUserParam(System, TEXT("ns_user_param_add"), Op, Error));
        TestTrue(TEXT("no apply error"), Error.IsEmpty());
        const FNiagaraVariable Variable(TypeFromName(Types[Index]), FName(*(FString(TEXT("User.")) + Names[Index])));
        TestTrue(TEXT("user parameter exists"), System->GetExposedParameters().IndexOf(Variable) != INDEX_NONE);
        TestEqual(TEXT("user parameter null round trip"), ParameterValueText(System->GetExposedParameters(), Variable), FString(TEXT("None")));
    }
    TArray<FNiagaraVariable> Variables;
    System->GetExposedParameters().GetParameters(Variables);
    TestEqual(TEXT("repeated additions do not duplicate parameters"), Variables.Num(), 3);
    return true;
}

#endif
