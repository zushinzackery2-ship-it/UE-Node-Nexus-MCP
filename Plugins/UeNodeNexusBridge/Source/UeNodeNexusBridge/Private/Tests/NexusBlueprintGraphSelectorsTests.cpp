#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Transcode/UeNodeNexusBridgeTranscodeBlueprintShared.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusBlueprintGraphSelectors,
    "Nexus.Issues3.Animation.GraphSelectors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusBlueprintGraphSelectors::RunTest(const FString& Parameters)
{
    auto* Blueprint = NewObject<UBlueprint>();
    auto* Root = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
    Root->GraphGuid = FGuid::NewGuid();
    Blueprint->UbergraphPages.Add(Root);
    TArray<UEdGraph*> Rules;
    const TCHAR* Names[] =
    {
        TEXT("First"), TEXT("Second")
    };
    for (const TCHAR* Name : Names)
    {
        auto* Owner = NewObject<UEdGraphNode>(Root, FName(Name));
        Root->AddNode(Owner, false, false);
        auto* Rule = NewObject<UEdGraph>(Owner, TEXT("Transition"));
        Rule->GraphGuid = FGuid::NewGuid();
        Root->SubGraphs.Add(Rule);
        Rules.Add(Rule);
    }
    TestEqual(TEXT("top level names remain supported"), FindBlueprintGraph(Blueprint, TEXT("EventGraph")), Root);
    TestNull(TEXT("ambiguous short name is rejected"), FindBlueprintGraph(Blueprint, TEXT("Transition")));
    TestNull(TEXT("unknown name is rejected"), FindBlueprintGraph(Blueprint, TEXT("Missing")));
    for (UEdGraph* Rule : Rules)
    {
        TestEqual(TEXT("absolute path resolves its graph"), FindBlueprintGraph(Blueprint, Rule->GetPathName()), Rule);
        TestEqual(TEXT("relative path resolves its graph"), FindBlueprintGraph(Blueprint, Rule->GetPathName(Blueprint)), Rule);
        TestEqual(TEXT("GUID resolves its graph"), FindBlueprintGraph(Blueprint, Rule->GraphGuid.ToString()), Rule);
        const auto Raw = Transcode::GraphJson(Blueprint, Rule, TEXT("graph"));
        TestEqual(TEXT("nested graph export has a unique selector"),
            Raw->GetStringField(TEXT("name")), Rule->GetPathName(Blueprint));
    }
    return true;
}

#endif
