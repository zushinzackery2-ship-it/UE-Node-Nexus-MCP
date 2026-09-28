#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

static UBlueprint* ScheduleBlueprint()
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusSchedule"));
    return FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}

static void ApplySchedulePlan(UBlueprint* Blueprint, const FString& Text, FApplyContext& Context)
{
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
    Context.bDryRun = false;
    ApplyBlueprintPlan(Blueprint, Plan, Context);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGraphSchedule,
    "Nexus.Issues3.Blueprint.WildcardSchedule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusConstructionReuse,
    "Nexus.Issues3.Blueprint.ConstructionReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusConstructionReuse::RunTest(const FString& Parameters)
{
    auto* Blueprint = ScheduleBlueprint();
    auto* Original = FindBlueprintGraph(Blueprint, TEXT("UserConstructionScript"));
    FApplyContext Context;
    ApplySchedulePlan(Blueprint, TEXT(R"([
        {"op":"bp_function_add","name":"UserConstructionScript","reuse_builtin":true,"signature":{"name":"UserConstructionScript","inputs":[],"outputs":[],"flags":["Public"]}}
    ])"), Context);
    TestEqual(TEXT("built-in graph reused"), Context.Failures.Num(), 0);
    TestTrue(TEXT("original graph identity retained"), Original && Original == FindBlueprintGraph(Blueprint, TEXT("UserConstructionScript")));
    ApplySchedulePlan(Blueprint, TEXT(R"([
        {"op":"bp_function_add","name":"UserConstructionScript"}
    ])"), Context);
    TestEqual(TEXT("ordinary duplicate creation remains rejected"), Context.Failures.Num(), 1);
    return true;
}

bool FNexusGraphSchedule::RunTest(const FString& Parameters)
{
    auto* Blueprint = ScheduleBlueprint();
    FApplyContext Context;
    ApplySchedulePlan(Blueprint, TEXT(R"([
        {"op":"bp_function_add","name":"Iterate","signature":{"name":"Iterate","inputs":[{"name":"Receivers","type":{"category":"object","subobject":"/Script/Engine.PrimitiveComponent","container":"array"}}],"outputs":[],"flags":["Public"]}},
        {"op":"create_node","graph":"Iterate","id":"loop","class":"K2Node_MacroInstance","positional":["StandardMacros.ForEachLoop"]},
        {"op":"create_node","graph":"Iterate","id":"cast","class":"K2Node_DynamicCast","positional":["/Script/Engine.PrimitiveComponent"]},
        {"op":"connect_pins","graph":"Iterate","from":"loop","from_pin":"Array Element","to":"cast","to_pin":"Object"},
        {"op":"connect_pins","graph":"Iterate","from":"entry","from_pin":"Receivers","to":"loop","to_pin":"Array"}
    ])"), Context);
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    TestEqual(TEXT("type anchor scheduled before dependent wildcard connection"), Context.Failures.Num(), 0);
    auto* Graph = FindBlueprintGraph(Blueprint, TEXT("Iterate"));
    auto* Cast = ResolveGraphNode(Blueprint, Graph, Context, TEXT("cast"));
    if (TestNotNull(TEXT("cast exists"), Cast))
    {
        auto* Input = ResolvePin(Cast, TEXT("Object"), EGPD_Input);
        TestTrue(TEXT("cast input is connected"), Input && Input->LinkedTo.Num() == 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFunctionRefresh,
    "Nexus.Issues3.Blueprint.FunctionCallRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFunctionRefresh::RunTest(const FString& Parameters)
{
    auto* Blueprint = ScheduleBlueprint();
    FApplyContext Context;
    ApplySchedulePlan(Blueprint, TEXT(R"([
        {"op":"bp_function_add","name":"Compute","signature":{"name":"Compute","inputs":[],"outputs":[],"flags":["Public"]}},
        {"op":"create_node","graph":"EventGraph","id":"call","class":"K2Node_CallFunction","positional":["self.Compute"]},
        {"op":"create_node","graph":"EventGraph","id":"branch","class":"K2Node_IfThenElse"}
    ])"), Context);
    TestEqual(TEXT("initial call created"), Context.Failures.Num(), 0);
    ApplySchedulePlan(Blueprint, TEXT(R"([
        {"op":"bp_function_signature_set","name":"Compute","signature":{"name":"Compute","inputs":[],"outputs":[{"name":"bComplete","type":{"category":"bool"}}],"flags":["Public"]}},
        {"op":"connect_pins","graph":"EventGraph","from":"call","from_pin":"bComplete","to":"branch","to_pin":"Condition"}
    ])"), Context);
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    TestEqual(TEXT("existing call exposes new output before connection"), Context.Failures.Num(), 0);
    auto* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    auto* Call = ResolveGraphNode(Blueprint, Graph, Context, TEXT("call"));
    if (TestNotNull(TEXT("call retained"), Call))
    {
        auto* Output = ResolvePin(Call, TEXT("bComplete"), EGPD_Output);
        TestTrue(TEXT("new output connected"), Output && Output->LinkedTo.Num() == 1);
    }
    return true;
}

#endif
