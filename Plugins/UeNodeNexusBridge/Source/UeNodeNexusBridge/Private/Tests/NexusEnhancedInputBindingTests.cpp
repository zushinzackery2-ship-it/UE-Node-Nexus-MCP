#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/Input/NexusEnhancedInputBinding.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "InputAction.h"
#include "K2Node_EnhancedInputAction.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusEnhancedInputBinding,
    "Nexus.Issues3.Character.EnhancedInputBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusEnhancedInputBinding::RunTest(const FString& Parameters)
{
    UEdGraph* Graph = NewObject<UEdGraph>();
    Graph->Schema = UEdGraphSchema_K2::StaticClass();
    auto* Node = NewObject<UK2Node_EnhancedInputAction>(Graph);
    auto* Action = NewObject<UInputAction>();
    Action->ValueType = EInputActionValueType::Axis2D;
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("input_action"), Action->GetPathName());
    FString Error;
    UClass* Class = Node->GetClass();
    TestTrue(TEXT("typed creation preview accepts an action"),
        ValidateBlueprintNodeCreateConfig(Class, nullptr, Graph, Payload, Error));
    TestNull(TEXT("preview does not mutate node"), Node->InputAction.Get());
    TestTrue(TEXT("shared creation binds action"),
        ConfigureCreatedBlueprintNode(Node, nullptr, Payload, Error));
    TestEqual(TEXT("action is retained"), Node->InputAction.Get(), static_cast<const UInputAction*>(Action));
    Node->AllocateDefaultPins();
    const UEdGraphPin* Value = Node->FindPin(TEXT("ActionValue"));
    TestNotNull(TEXT("action value pin exists"), Value);
    if (Value)
    {
        TestEqual(TEXT("Axis2D action produces a Vector2D pin"),
            Value->PinType.PinSubCategoryObject.Get(), static_cast<UObject*>(TBaseStructure<FVector2D>::Get()));
    }
    TestNotNull(TEXT("completion pin exists"), Node->FindPin(TEXT("Completed")));
    Payload->SetStringField(TEXT("input_action"), Graph->GetPathName());
    TestFalse(TEXT("wrong resource class is refused in preview"),
        ValidateBlueprintNodeCreateConfig(Class, nullptr, Graph, Payload, Error));
    TestFalse(TEXT("wrong resource class is refused in apply"),
        ConfigureCreatedBlueprintNode(Node, nullptr, Payload, Error));
    TestEqual(TEXT("failed writes preserve binding"), Node->InputAction.Get(), static_cast<const UInputAction*>(Action));
    Payload->RemoveField(TEXT("input_action"));
    TestFalse(TEXT("missing action is refused"), ConfigureCreatedBlueprintNode(Node, nullptr, Payload, Error));
    return true;
}

#endif
