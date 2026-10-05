#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "Materials/Material.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"

using namespace UeNodeNexusBridge::VfxTranscode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraSafetyValues,
    "Nexus.Safety.NiagaraInvalidValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraSafetyValues::RunTest(const FString& Parameters)
{
    auto* System = NewObject<UNiagaraSystem>();
    auto& Store = System->GetExposedParameters();
    const FNiagaraVariable Floating(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Float"));
    const FNiagaraVariable Integer(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.Int"));
    const FNiagaraVariable Boolean(FNiagaraTypeDefinition::GetBoolDef(), TEXT("User.Bool"));
    const FNiagaraVariable Vector(FNiagaraTypeDefinition::GetVec3Def(), TEXT("User.Vector"));
    const FNiagaraVariable Material(FNiagaraTypeDefinition(UMaterial::StaticClass()), TEXT("User.Material"));
    TestTrue(TEXT("float setup"), SetParameterValueText(Store, Floating, TEXT("0.75"), true));
    TestTrue(TEXT("int setup"), SetParameterValueText(Store, Integer, TEXT("17"), true));
    TestTrue(TEXT("bool setup"), SetParameterValueText(Store, Boolean, TEXT("False"), true));
    TestTrue(TEXT("vector setup"), SetParameterValueText(Store, Vector, TEXT("1,2,3"), true));
    auto* Object = NewObject<UMaterial>();
    TestTrue(TEXT("object setup"), SetParameterValueText(Store, Material, Object->GetPathName(), true));

    TestFalse(TEXT("malformed number rejected"), SetParameterValueText(Store, Floating, TEXT("broken"), false));
    TestFalse(TEXT("non-finite number rejected"), SetParameterValueText(Store, Floating, TEXT("NaN"), false));
    TestFalse(TEXT("overflow rejected"), SetParameterValueText(Store, Integer, TEXT("2147483648"), false));
    TestFalse(TEXT("trailing data rejected"), SetParameterValueText(Store, Integer, TEXT("17oops"), false));
    TestFalse(TEXT("invalid bool rejected"), SetParameterValueText(Store, Boolean, TEXT("tomato"), false));
    TestFalse(TEXT("invalid vector component rejected"), SetParameterValueText(Store, Vector, TEXT("1,bad,3"), false));
    TestFalse(TEXT("excess components rejected"), SetParameterValueText(Store, Vector, TEXT("1,2,3,4"), false));
    TestFalse(TEXT("missing object is not silently cleared"), SetParameterValueText(Store, Material,
        TEXT("/Game/NexusSafety/Missing.Missing"), false));
    TestEqual(TEXT("float preserved"), Store.GetParameterValue<float>(Floating), 0.75f);
    TestEqual(TEXT("int preserved"), Store.GetParameterValue<int32>(Integer), 17);
    TestFalse(TEXT("bool preserved"), Store.GetParameterValue<FNiagaraBool>(Boolean).GetValue());
    TestEqual(TEXT("vector preserved"), Store.GetParameterValue<FVector3f>(Vector), FVector3f(1, 2, 3));
    TestEqual(TEXT("object preserved"), Store.GetUObject(Material), static_cast<UObject*>(Object));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraSafetyType,
    "Nexus.Safety.NiagaraUserTypeContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraSafetyType::RunTest(const FString& Parameters)
{
    auto* System = NewObject<UNiagaraSystem>();
    auto Op = MakeShared<FJsonObject>();
    Op->SetStringField(TEXT("name"), TEXT("Contract"));
    Op->SetStringField(TEXT("type"), TEXT("int"));
    Op->SetStringField(TEXT("value"), TEXT("19"));
    FString Error;
    TestTrue(TEXT("declare int"), ApplyUserParam(System, TEXT("ns_user_param_add"), Op, Error));
    Op->SetStringField(TEXT("type"), TEXT("float"));
    Op->SetStringField(TEXT("value"), TEXT("2.5"));
    TestFalse(TEXT("same name different type refused"), ApplyUserParam(System, TEXT("ns_user_param_add"), Op, Error));
    TestFalse(TEXT("type mismatch has diagnostics"), Error.IsEmpty());
    const FNiagaraVariable Variable(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.Contract"));
    TestEqual(TEXT("original value preserved"), System->GetExposedParameters().GetParameterValue<int32>(Variable), 19);
    return true;
}
#endif
