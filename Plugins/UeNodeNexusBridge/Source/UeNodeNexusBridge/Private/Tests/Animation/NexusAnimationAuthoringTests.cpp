#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/BlendSpace1D.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "Blueprint/Properties/NexusBlueprintNodeProperties.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UObject/UnrealType.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAnimationProperties,
    "Nexus.Issues3.Animation.NodeProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimationProperties::RunTest(const FString& Parameters)
{
    auto* Node = NewObject<UAnimGraphNode_BlendSpacePlayer>();
    auto* BlendSpace = NewObject<UBlendSpace1D>();
    FProperty* Property = Node->GetClass()->FindPropertyByName(TEXT("Node"));
    const FString Initial = Transcode::ExportPropertyValue(Node, Property);
    const auto Value = MakeShared<FJsonValueString>(BlendSpace->GetPathName());
    FString Before;
    FString After;
    FString Error;
    TestTrue(TEXT("resource path validates"), ApplyBlueprintNodeProperty(
        Node, TEXT("Node.BlendSpace"), Value, true, false, Before, After, Error));
    TestEqual(TEXT("dry run preserves value"), Transcode::ExportPropertyValue(Node, Property), Initial);
    TestTrue(TEXT("resource is written"), ApplyBlueprintNodeProperty(
        Node, TEXT("Node.BlendSpace"), Value, false, false, Before, After, Error));
    const FString Written = Transcode::ExportPropertyValue(Node, Property);
    TestTrue(TEXT("resource reads back"), Written.Contains(BlendSpace->GetPathName()));

    auto Params = MakeShared<FJsonObject>();
    auto Struct = MakeShared<FJsonObject>();
    Struct->SetBoolField(TEXT("bLoop"), false);
    Params->SetObjectField(TEXT("Node"), Struct);
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetObjectField(TEXT("params"), Params);
    TestTrue(TEXT("creation config accepts partial struct"), ConfigureBlueprintNodeProperties(Node, Payload, false, Error));
    const FString Configured = Transcode::ExportPropertyValue(Node, Property);
    TestTrue(TEXT("partial struct preserves reference"), Configured.Contains(BlendSpace->GetPathName()));
    TestTrue(TEXT("partial struct changes loop"), Configured.Contains(TEXT("bLoop=False")));

    TestFalse(TEXT("unknown property is rejected"), ApplyBlueprintNodeProperty(
        Node, TEXT("Node.NotAField"), Value, false, false, Before, After, Error));
    TestFalse(TEXT("wrong resource class is rejected"), ApplyBlueprintNodeProperty(
        Node, TEXT("Node.BlendSpace"), MakeShared<FJsonValueString>(Node->GetPathName()),
        false, false, Before, After, Error));
    TestEqual(TEXT("failed writes preserve node"), Transcode::ExportPropertyValue(Node, Property), Configured);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAnimationMirror,
    "Nexus.Issues3.Animation.BlueprintMirror",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimationMirror::RunTest(const FString& Parameters)
{
    auto* Blueprint = NewObject<UAnimBlueprint>();
    Blueprint->ParentClass = UAnimInstance::StaticClass();
    auto* Graph = NewObject<UEdGraph>(Blueprint, TEXT("AnimGraph"));
    Graph->Schema = UAnimationGraphSchema::StaticClass();
    Blueprint->FunctionGraphs.Add(Graph);
    auto* Node = NewObject<UAnimGraphNode_BlendSpacePlayer>(Graph);
    Node->CreateNewGuid();
    Graph->AddNode(Node, false, false);
    Node->AllocateDefaultPins();
    TestEqual(TEXT("animation blueprint uses graph mirror"),
        Transcode::KindForClass(Blueprint->GetClass()), FString(TEXT("blueprint")));
    auto Raw = Transcode::BuildBlueprintRaw(Blueprint);
    const auto& Graphs = Raw->GetObjectField(TEXT("blueprint"))->GetArrayField(TEXT("graphs"));
    TestEqual(TEXT("animation graph is exported"), Graphs.Num(), 1);
    const auto& Nodes = Graphs[0]->AsObject()->GetArrayField(TEXT("nodes"));
    TestEqual(TEXT("animation node is preserved"), Nodes.Num(), 1);
    TestFalse(TEXT("animation node remains opaque"), Nodes[0]->AsObject()->GetBoolField(TEXT("supported")));
    TestFalse(TEXT("opaque node retains native serialization"), Nodes[0]->AsObject()->GetStringField(TEXT("t3d")).IsEmpty());
    return true;
}

#endif
