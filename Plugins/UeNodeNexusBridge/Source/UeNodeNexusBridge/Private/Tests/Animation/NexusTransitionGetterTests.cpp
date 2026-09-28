#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "AnimationTransitionSchema.h"
#include "Blueprint/Animation/NexusTransitionGetterBinding.h"
#include "Blueprint/Properties/NexusBlueprintNodeProperties.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_TransitionRuleGetter.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusTransitionGetterBinding,
    "Nexus.Issues3.Animation.TransitionGetterBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusTransitionGetterBinding::RunTest(const FString& Parameters)
{
    auto* Blueprint = NewObject<UAnimBlueprint>();
    auto* Graph = NewObject<UEdGraph>(Blueprint, TEXT("Transition"));
    Graph->Schema = UAnimationTransitionSchema::StaticClass();
    Blueprint->FunctionGraphs.Add(Graph);
    auto* Node = NewObject<UK2Node_TransitionRuleGetter>(Graph);
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("getter_type"), TEXT("CurrentState_ElapsedTime"));
    FString Error;
    UClass* Class = Node->GetClass();
    const auto Initial = Class->GetDefaultObject<UK2Node_TransitionRuleGetter>()->GetterType;
    TestTrue(TEXT("typed preview accepts elapsed time"),
        ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
    TestEqual(TEXT("preview preserves CDO"),
        Class->GetDefaultObject<UK2Node_TransitionRuleGetter>()->GetterType, Initial);
    TestTrue(TEXT("typed creation binds the native getter"),
        ConfigureCreatedBlueprintNode(Node, Blueprint, Payload, Error));
    TestEqual(TEXT("getter type is retained"), Node->GetterType.GetValue(), ETransitionGetter::CurrentState_ElapsedTime);
    Node->AllocateDefaultPins();
    TestNotNull(TEXT("compiler-bound getter has output"), Node->FindPin(TEXT("Output")));
    TestNull(TEXT("authored getter has no numeric machine index"), Node->FindPin(TEXT("MachineIndex")));

    FString Before;
    FString After;
    TestFalse(TEXT("generic property protection remains"), ApplyBlueprintNodeProperty(Node, TEXT("GetterType"),
        MakeShared<FJsonValueString>(TEXT("CurrentState_GetBlendWeight")), false, false, Before, After, Error));
    const FString InvalidNames[] =
    {
        TEXT("Invalid"), TEXT("AnimationAsset_GetCurrentTime"), TEXT("ArbitraryState_GetBlendWeight")
    };
    for (const FString& Invalid : InvalidNames)
    {
        Payload->SetStringField(TEXT("getter_type"), Invalid);
        TestFalse(TEXT("unbound getter is rejected in preview"),
            ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
        TestFalse(TEXT("unbound getter is rejected in creation"),
            ConfigureCreatedBlueprintNode(Node, Blueprint, Payload, Error));
        TestEqual(TEXT("rejection preserves binding"), Node->GetterType.GetValue(), ETransitionGetter::CurrentState_ElapsedTime);
    }
    Payload->RemoveField(TEXT("getter_type"));
    TestFalse(TEXT("missing getter type is rejected"),
        ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
    const FString OtherNames[] =
    {
        TEXT("CurrentState_GetBlendWeight"), TEXT("CurrentTransitionDuration")
    };
    for (const FString& Name : OtherNames)
    {
        Payload->SetStringField(TEXT("getter_type"), Name);
        TestTrue(TEXT("other context-bound getters validate"),
            ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
        TestTrue(TEXT("other context-bound getters bind"),
            ConfigureCreatedBlueprintNode(Node, Blueprint, Payload, Error));
    }
    Graph->Schema = UEdGraphSchema_K2::StaticClass();
    TestFalse(TEXT("ordinary Blueprint graph is refused"),
        ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
    TestFalse(TEXT("missing graph is refused"), ConfigureTransitionGetter(Node, nullptr, Payload, true, Error));
    return true;
}

#endif
