#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Niagara/Modules/Binding/NexusNiagaraInputBinding.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraUserLinkTypes,
    "Nexus.Niagara.UserLink.TypesAndMissingParameter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraUserLinkTypes::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    UNiagaraSystem* System = NewObject<UNiagaraSystem>();
    FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    const FNiagaraVariable Rate(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SpawnRate"));
    const FNiagaraVariable Wrong(FNiagaraTypeDefinition::GetIntDef(), TEXT("User.WrongRate"));
    TestTrue(TEXT("declare existing typed user parameter"), SetParameterValueText(Store, Rate, TEXT("8000"), true));
    TestTrue(TEXT("declare mismatched user type"), SetParameterValueText(Store, Wrong, TEXT("7"), true));
    const TArray<uint8> Before = Store.GetParameterDataArray();
    FInputBindingError Error;
    TestTrue(TEXT("float user can bind to float input"), ValidateUserParameter(Store, TEXT("User.SpawnRate"), Rate.GetType(), Error));
    TestFalse(TEXT("int user cannot bind to float input"), ValidateUserParameter(Store, TEXT("User.WrongRate"), Rate.GetType(), Error));
    TestEqual(TEXT("specific type failure code"), Error.Code, FString(TEXT("user_parameter_type_mismatch")));
    TestTrue(TEXT("expected type in diagnostic"), Error.Message.Contains(Rate.GetType().GetName()));
    TestTrue(TEXT("actual type in diagnostic"), Error.Message.Contains(Wrong.GetType().GetName()));
    TestTrue(TEXT("parameter identity in diagnostic"), Error.Message.Contains(TEXT("User.WrongRate")));
    TestFalse(TEXT("missing user cannot be silently created"), ValidateUserParameter(Store, TEXT("User.Missing"), Rate.GetType(), Error));
    TestEqual(TEXT("specific missing parameter code"), Error.Code, FString(TEXT("user_parameter_missing")));
    TestTrue(TEXT("validation preserves store bytes"), Store.GetParameterDataArray() == Before);
    TArray<FNiagaraVariable> Users;
    Store.GetUserParameters(Users);
    TestEqual(TEXT("validation preserves declarations"), Users.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraUserLinkNames,
    "Nexus.Niagara.UserLink.NamespaceAndAmbiguousTypes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraUserLinkNames::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::VfxTranscode;
    UNiagaraSystem* System = NewObject<UNiagaraSystem>();
    FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
    const FNiagaraVariable Rate(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Rain.Rate"));
    TestTrue(TEXT("hierarchical user declaration"), SetParameterValueText(Store, Rate, TEXT("2800"), true));
    FInputBindingError Error;
    TestTrue(TEXT("full hierarchical handle accepted"), ValidateUserParameter(Store, TEXT("User.Rain.Rate"), Rate.GetType(), Error));
    TArray<FString> Invalid;
    Invalid.Add(TEXT("Emitter.Rate"));
    Invalid.Add(TEXT("User."));
    Invalid.Add(TEXT("User.Rain..Rate"));
    Invalid.Add(TEXT("User.Rain."));
    for (const FString& Name : Invalid)
    {
        TestFalse(TEXT("invalid handle rejected"), ValidateUserParameter(Store, Name, Rate.GetType(), Error));
        TestEqual(TEXT("invalid handle code"), Error.Code, FString(TEXT("invalid_user_link")));
    }
    const FNiagaraVariable Duplicate(FNiagaraTypeDefinition::GetIntDef(), Rate.GetName());
    TestTrue(TEXT("prepare conflicting type with same name"), SetParameterValueText(Store, Duplicate, TEXT("2"), true));
    TestFalse(TEXT("matching one of two types is ambiguous"), ValidateUserParameter(Store, TEXT("User.Rain.Rate"), Rate.GetType(), Error));
    TestTrue(TEXT("ambiguous name count retained"), Error.Message.Contains(TEXT("name_matches=2")));
    return true;
}
#endif
