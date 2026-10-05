#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

static UBlueprint* RemovalBlueprint()
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusRemoval"));
    return FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}

static void ApplyRemovalPlan(UBlueprint* Blueprint, const FString& Text, FApplyContext& Context)
{
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
    Context.bDryRun = false;
    ApplyBlueprintPlan(Blueprint, Plan, Context);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMemberRemoval,
    "Nexus.Issues4.Blueprint.MemberRemovalOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMemberRemoval::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = RemovalBlueprint();
    FApplyContext Context;
    ApplyRemovalPlan(Blueprint, TEXT(R"([
        {
            "op":"bp_variable_add", "name":"OldValue", "default":"1",
            "type":{"category":"real","subcategory":"float"}
        },
        {
            "op":"create_node", "graph":"EventGraph", "id":"old_read",
            "class":"VariableGet", "positional":["OldValue"]
        },
        {
            "op":"create_node", "graph":"EventGraph", "id":"old_write",
            "class":"VariableSet", "positional":["OldValue"]
        }
    ])"), Context);
    TestEqual(TEXT("source variable readers and writers created"), Context.Failures.Num(), 0);
    ApplyRemovalPlan(Blueprint, TEXT(R"([
        {
            "op":"bp_variable_remove", "name":"OldValue"
        },
        {
            "op":"bp_variable_add", "name":"NewValue", "default":"2",
            "type":{"category":"real","subcategory":"float"}
        },
        {
            "op":"delete_node", "graph":"EventGraph", "id":"old_read"
        },
        {
            "op":"delete_node", "graph":"EventGraph", "id":"old_write"
        },
        {
            "op":"create_node", "graph":"EventGraph", "id":"new_read",
            "class":"VariableGet", "positional":["NewValue"]
        }
    ])"), Context);
    TestEqual(TEXT("explicit node removal precedes native member side effects"), Context.Failures.Num(), 0);
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    TestNull(TEXT("old reader removed"), ResolveGraphNode(Blueprint, Graph, Context, TEXT("old_read")));
    TestNull(TEXT("old writer removed"), ResolveGraphNode(Blueprint, Graph, Context, TEXT("old_write")));
    TestNotNull(TEXT("new member available to new node"), ResolveGraphNode(Blueprint, Graph, Context, TEXT("new_read")));
    TestEqual(TEXT("old declaration removed"), FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, FName(TEXT("OldValue"))), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRemovalMissing,
    "Nexus.Issues4.Blueprint.MissingRemovalRejected",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRemovalMissing::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = RemovalBlueprint();
    FApplyContext Context;
    ApplyRemovalPlan(Blueprint, TEXT(R"([
        {
            "op":"delete_node", "graph":"EventGraph", "id":"missing"
        }
    ])"), Context);
    TestEqual(TEXT("missing explicit deletion remains an error"), Context.Failures.Num(), 1);
    return true;
}

#endif
