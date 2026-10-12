#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_CallFunction.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSequenceShrink,
    "Nexus.Issues13.Blueprint.SequenceShrink",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSequenceShrink::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusSequence")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    UEdGraph* Graph = Blueprint->UbergraphPages[0];
    auto* Sequence = NewObject<UK2Node_ExecutionSequence>(Graph);
    Graph->AddNode(Sequence);
    Sequence->AllocateDefaultPins();
    FString Error;
    TestTrue(TEXT("grow sequence"), SetDynamicPins(Sequence, 4, Error));
    TestNotNull(TEXT("fourth output exists"), Sequence->GetThenPinGivenIndex(3));
    TestTrue(TEXT("shrink sequence"), SetDynamicPins(Sequence, 3, Error));
    TestNull(TEXT("fourth output removed"), Sequence->GetThenPinGivenIndex(3));
    Sequence->ReconstructNode();
    TestNotNull(TEXT("third output survives reconstruction"), Sequence->GetThenPinGivenIndex(2));
    TestNull(TEXT("removed output stays absent after reconstruction"), Sequence->GetThenPinGivenIndex(3));
    TestFalse(TEXT("native minimum is enforced"), SetDynamicPins(Sequence, 1, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLocalFunctionDeclaration,
    "Nexus.Issues13.Blueprint.LocalFunctionDeclaration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusLocalFunctionDeclaration::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(UBlueprintFunctionLibrary::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusLocalCalls")),
        BPTYPE_FunctionLibrary, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TArray<TSharedPtr<FJsonValue>> Plan;
    const FString Text = FString::Printf(TEXT(R"([
        {"op":"bp_function_add","name":"Update","signature":{"inputs":[],"outputs":[],"flags":["Public","Static"]}},
        {"op":"create_node","id":"sync","graph":"Update","class":"K2Node_CallFunction",
         "positional":["%s.Synchronize"]},
        {"op":"bp_function_add","name":"Synchronize","signature":{"inputs":[],"outputs":[],"flags":["Private","Static"]}},
        {"op":"connect_pins","graph":"Update","from":"entry","from_pin":"then","to":"sync","to_pin":"execute"},
        {"op":"connect_pins","graph":"Update","from":"sync","from_pin":"then","to":"result","to_pin":"execute"},
        {"op":"connect_pins","graph":"Synchronize","from":"entry","from_pin":"then","to":"result","to_pin":"execute"}
    ])"), *Blueprint->GeneratedClass->GetPathName());
    if (!TestTrue(TEXT("valid plan"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan)))
    {
        return false;
    }
    FApplyContext Context;
    ApplyBlueprintPlan(Blueprint, Plan, Context);
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    TestEqual(TEXT("every declaration and call applies"), Context.Failures.Num(), 0);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("generated class contains the new function"), Blueprint->GeneratedClass->FindFunctionByName(TEXT("Synchronize")) != nullptr);
    TestTrue(TEXT("function library compiles"), Blueprint->Status == BS_UpToDate);
    return true;
}

#endif
