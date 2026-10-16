#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeDiagnostics.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGetArrayItemAlias,
    "Nexus.Blueprint.GetArrayItemAlias",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusGetArrayItemAlias::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusGetArrayItemAlias")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    FApplyContext Context;
    Context.bDryRun = false;
    const auto Apply = [Blueprint, &Context](const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Plan;
        check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
        ApplyBlueprintPlan(Blueprint, Plan, Context);
    };
    // One int array of real element type, indexed once through a literal, read
    // through both the author aliases and the engine-generated names.
    Apply(TEXT(R"([
        {"op":"bp_variable_add","name":"Values","type":{"category":"int","container":"array"}},
        {"op":"create_node","id":"values","class":"K2Node_VariableGet","positional":["Values"]},
        {"op":"create_node","id":"zero","class":"K2Node_CallFunction","positional":["/Script/Engine.KismetSystemLibrary.MakeLiteralInt"]},
        {"op":"create_node","id":"alias_item","class":"K2Node_GetArrayItem"},
        {"op":"create_node","id":"native_item","class":"K2Node_GetArrayItem"},
        {"op":"connect_pins","from":"values","from_pin":"Values","to":"alias_item","to_pin":"TargetArray"},
        {"op":"connect_pins","from":"zero","from_pin":"ReturnValue","to":"alias_item","to_pin":"Index"},
        {"op":"connect_pins","from":"values","from_pin":"Values","to":"native_item","to_pin":"Array"},
        {"op":"connect_pins","from":"zero","from_pin":"ReturnValue","to":"native_item","to_pin":"Dimension 1"}
    ])"));
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    if (!TestEqual(TEXT("aliased and native spellings both connect"), Context.Failures.Num(), 0))
    {
        return false;
    }
    auto* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    auto* Aliased = ResolveGraphNode(Blueprint, Graph, Context, TEXT("alias_item"));
    auto* Native = ResolveGraphNode(Blueprint, Graph, Context, TEXT("native_item"));
    if (TestNotNull(TEXT("aliased node exists"), Aliased) && TestNotNull(TEXT("native node exists"), Native))
    {
        TestNotNull(TEXT("TargetArray resolves to the array input"), ResolvePin(Aliased, TEXT("TargetArray"), EGPD_Input));
        TestNotNull(TEXT("Index resolves to the dimension input"), ResolvePin(Aliased, TEXT("Index"), EGPD_Input));
        TestNotNull(TEXT("Item resolves to the element output"), ResolvePin(Aliased, TEXT("Item"), EGPD_Output));
        TestNotNull(TEXT("Array stays reachable by its engine name"), ResolvePin(Native, TEXT("Array"), EGPD_Input));
        TestNotNull(TEXT("Dimension 1 stays reachable by its engine name"), ResolvePin(Native, TEXT("Dimension 1"), EGPD_Input));
        TestNotNull(TEXT("Output stays reachable by its engine name"), ResolvePin(Native, TEXT("Output"), EGPD_Output));
        auto* ArrayPin = ResolvePin(Aliased, TEXT("Array"), EGPD_Input);
        if (ArrayPin != nullptr)
        {
            TestEqual(TEXT("the array input took the int element type"), ArrayPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Int);
            TestEqual(TEXT("the alias connected the real pin"), ArrayPin->LinkedTo.Num(), 1);
        }
    }
    const auto Compiled = CollectAssetCompileDiagnostics(Blueprint, Blueprint->GetPathName(), false);
    TestEqual(TEXT("the graph compiles"), Compiled.ErrorCount, 0);
    return true;
}

#endif
