#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeDiagnostics.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusArrayRetype,
    "Nexus.Issues6.Blueprint.ArrayRetype",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusArrayRetype::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusArrayRetype")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    FApplyContext Context;
    Context.bDryRun = false;
    const auto Apply = [Blueprint, &Context](const FString& Text)
    {
        TArray<TSharedPtr<FJsonValue>> Plan;
        check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
        ApplyBlueprintPlan(Blueprint, Plan, Context);
    };
    Apply(TEXT(R"([
        {"op":"bp_variable_add","name":"Targets","type":{"category":"object","subobject":"/Script/Engine.PrimitiveComponent","container":"array"}},
        {"op":"create_node","id":"get","class":"K2Node_VariableGet","positional":["Targets"]},
        {"op":"create_node","id":"loop","class":"K2Node_MacroInstance","positional":["StandardMacros.ForEachLoop"]},
        {"op":"create_node","id":"clear","class":"K2Node_CallFunction","positional":["/Script/Engine.KismetArrayLibrary.Array_Clear"]},
        {"op":"create_node","id":"add","class":"K2Node_CallFunction","positional":["/Script/Engine.KismetArrayLibrary.Array_AddUnique"]},
        {"op":"connect_pins","from":"get","from_pin":"Targets","to":"loop","to_pin":"Array"},
        {"op":"connect_pins","from":"get","from_pin":"Targets","to":"clear","to_pin":"TargetArray"},
        {"op":"connect_pins","from":"get","from_pin":"Targets","to":"add","to_pin":"TargetArray"},
        {"op":"connect_pins","from":"loop","from_pin":"Array Element","to":"add","to_pin":"NewItem"}
    ])"));
    if (!TestEqual(TEXT("initial specialized graph created"), Context.Failures.Num(), 0))
    {
        return false;
    }
    auto* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    auto* Loop = ResolveGraphNode(Blueprint, Graph, Context, TEXT("loop"));
    const FGuid LoopGuid = Loop->NodeGuid;
    Apply(TEXT(R"([
        {"op":"bp_variable_set","name":"Targets","type":{"category":"struct","subobject":"/Script/Engine.HitResult","container":"array"}}
    ])"));
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    TestEqual(TEXT("variable edit applied"), Context.Failures.Num(), 0);
    TestEqual(TEXT("loop GUID retained"), Loop->NodeGuid, LoopGuid);
    const TPair<FString, FString> Inputs[] =
    {
        TPair<FString, FString>(TEXT("loop"), TEXT("Array")),
        TPair<FString, FString>(TEXT("clear"), TEXT("TargetArray")),
        TPair<FString, FString>(TEXT("add"), TEXT("TargetArray")),
        TPair<FString, FString>(TEXT("add"), TEXT("NewItem"))
    };
    for (const auto& Input : Inputs)
    {
        auto* Node = ResolveGraphNode(Blueprint, Graph, Context, Input.Key);
        auto* Pin = ResolvePin(Node, Input.Value, EGPD_Input);
        if (TestNotNull(Input.Key + TEXT(" pin exists"), Pin))
        {
            TestEqual(Input.Key + TEXT(" category refreshed"), Pin->PinType.PinCategory, UEdGraphSchema_K2::PC_Struct);
            TestTrue(Input.Key + TEXT(" uses HitResult"), Pin->PinType.PinSubCategoryObject == FHitResult::StaticStruct());
            TestEqual(Input.Key + TEXT(" link retained"), Pin->LinkedTo.Num(), 1);
        }
    }
    const auto Compiled = CollectAssetCompileDiagnostics(Blueprint, Blueprint->GetPathName(), false);
    TestEqual(TEXT("retyped graph compiles"), Compiled.ErrorCount, 0);
    Apply(TEXT(R"([
        {"op":"bp_variable_set","name":"Targets","type":{"category":"object","subobject":"/Script/Engine.PrimitiveComponent","container":"array"}}
    ])"));
    TestEqual(TEXT("reverse type edit succeeds"), Context.Failures.Num(), 0);
    for (const auto& Input : Inputs)
    {
        auto* Node = ResolveGraphNode(Blueprint, Graph, Context, Input.Key);
        auto* Pin = ResolvePin(Node, Input.Value, EGPD_Input);
        if (TestNotNull(Input.Key + TEXT(" reverse pin exists"), Pin))
        {
            TestEqual(Input.Key + TEXT(" reverse category"), Pin->PinType.PinCategory, UEdGraphSchema_K2::PC_Object);
            TestEqual(Input.Key + TEXT(" reverse link retained"), Pin->LinkedTo.Num(), 1);
        }
    }
    return true;
}

#endif
