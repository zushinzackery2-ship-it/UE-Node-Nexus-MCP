#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

namespace
{
UBlueprint* MigrationBlueprint()
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusMigration"));
    return FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}

void ApplyMigration(UBlueprint* Blueprint, const FString& Text, UeNodeNexusBridge::Transcode::FApplyContext& Context)
{
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
    Context.bDryRun = false;
    UeNodeNexusBridge::Transcode::ApplyBlueprintPlan(Blueprint, Plan, Context);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInterfaceDisconnect,
    "Nexus.Issues7.Blueprint.InterfaceDisconnectBeforeReconstruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInterfaceDisconnect::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = MigrationBlueprint();
    UeNodeNexusBridge::Transcode::FApplyContext Context;
    ApplyMigration(Blueprint, TEXT(R"([
        {"op":"bp_variable_add","name":"Value","default":"1","type":{"category":"int"}},
        {"op":"bp_function_add","name":"Transition","signature":
            {"inputs":[{"name":"OldType","type":{"category":"int"}}],
             "outputs":[{"name":"Result","type":{"category":"int"}}]}},
        {"op":"create_node","id":"value","class":"VariableGet","positional":["Value"]},
        {"op":"create_node","id":"call","class":"CallFunction","positional":["self.Transition"]},
        {"op":"connect_pins","graph":"Transition","from":"entry","from_pin":"OldType","to":"result","to_pin":"Result"},
        {"op":"connect_pins","from":"value","from_pin":"Value","to":"call","to_pin":"OldType"}
    ])"), Context);
    TestEqual(TEXT("original entry/result and caller links exist"), Context.Failures.Num(), 0);
    ApplyMigration(Blueprint, TEXT(R"([
        {"op":"bp_function_signature_set","name":"Transition","signature":
            {"inputs":[{"name":"SpeedType","type":{"category":"int"}}],
             "outputs":[{"name":"Result","type":{"category":"int"}}]}},
        {"op":"disconnect_pins","graph":"Transition","from":"entry","from_pin":"OldType","to":"result","to_pin":"Result"},
        {"op":"disconnect_pins","from":"value","from_pin":"Value","to":"call","to_pin":"OldType"},
        {"op":"connect_pins","graph":"Transition","from":"entry","from_pin":"SpeedType","to":"result","to_pin":"Result"},
        {"op":"connect_pins","from":"value","from_pin":"Value","to":"call","to_pin":"SpeedType"}
    ])"), Context);
    TestEqual(TEXT("old pins disconnected before signature and caller rebuild"), Context.Failures.Num(), 0);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("migrated blueprint compiles"), Blueprint->Status == BS_UpToDate);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCustomEventDeclaration,
    "Nexus.Issues7.Blueprint.CustomEventDeclaredBeforeSelfCall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCustomEventDeclaration::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = MigrationBlueprint();
    UeNodeNexusBridge::Transcode::FApplyContext Context;
    ApplyMigration(Blueprint, TEXT(R"([
        {"op":"create_node","id":"call","class":"CallFunction","positional":["self.LoadExhibit"]},
        {"op":"create_node","id":"event","class":"CustomEvent","positional":["LoadExhibit","Index: int"]}
    ])"), Context);
    TestEqual(TEXT("same-plan event is callable regardless of graph order"), Context.Failures.Num(), 0);
    TestNotNull(TEXT("skeleton declares the custom event"), Blueprint->SkeletonGeneratedClass->FindFunctionByName(TEXT("LoadExhibit")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("event and self-call compile in one publication"), Blueprint->Status == BS_UpToDate);
    return true;
}
#endif
