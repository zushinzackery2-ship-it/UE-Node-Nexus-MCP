#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/Delegates/NexusDelegateBinding.h"
#include "Blueprint/UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClearDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transcode/Schema/NexusDelegateSchema.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "UObject/UnrealType.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

namespace
{
UBlueprint* DelegateBlueprint()
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusDelegates"));
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (FindBlueprintGraph(Blueprint, TEXT("EventGraph")) == nullptr)
    {
        auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"),
            UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
    }
    return Blueprint;
}

TArray<TSharedPtr<FJsonValue>> DelegatePlan()
{
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"([
        {"op":"bp_variable_add","name":"Count","type":{"category":"int"},"default":"0"},
        {"op":"bp_dispatcher_add","name":"Changed","signature":{"name":"Changed","inputs":[{"name":"Value","type":{"category":"int"}}],"outputs":[]}},
        {"op":"create_node","graph":"EventGraph","id":"listener","class":"K2Node_CustomEvent","positional":["OnValue","Value: int"]},
        {"op":"create_node","graph":"EventGraph","id":"bind_event","class":"K2Node_CustomEvent","positional":["Bind"]},
        {"op":"create_node","graph":"EventGraph","id":"remove_event","class":"K2Node_CustomEvent","positional":["Remove"]},
        {"op":"create_node","graph":"EventGraph","id":"clear_event","class":"K2Node_CustomEvent","positional":["Clear"]},
        {"op":"create_node","graph":"EventGraph","id":"emit_event","class":"K2Node_CustomEvent","positional":["Emit","Value: int"]},
        {"op":"create_node","graph":"EventGraph","id":"bind","class":"K2Node_AddDelegate","positional":["self.Changed"]},
        {"op":"create_node","graph":"EventGraph","id":"remove","class":"K2Node_RemoveDelegate","positional":["self.Changed"]},
        {"op":"create_node","graph":"EventGraph","id":"clear","class":"K2Node_ClearDelegate","positional":["self.Changed"]},
        {"op":"create_node","graph":"EventGraph","id":"emit","class":"K2Node_CallDelegate","positional":["self.Changed"]},
        {"op":"create_node","graph":"EventGraph","id":"count","class":"K2Node_VariableGet","positional":["Count"]},
        {"op":"create_node","graph":"EventGraph","id":"add","class":"K2Node_CallFunction","positional":["/Script/Engine.KismetMathLibrary.Add_IntInt"]},
        {"op":"create_node","graph":"EventGraph","id":"store","class":"K2Node_VariableSet","positional":["Count"]},
        {"op":"connect_pins","graph":"EventGraph","from":"listener","from_pin":"OutputDelegate","to":"bind","to_pin":"Delegate"},
        {"op":"connect_pins","graph":"EventGraph","from":"listener","from_pin":"OutputDelegate","to":"remove","to_pin":"Delegate"},
        {"op":"connect_pins","graph":"EventGraph","from":"bind_event","from_pin":"then","to":"bind","to_pin":"execute"},
        {"op":"connect_pins","graph":"EventGraph","from":"remove_event","from_pin":"then","to":"remove","to_pin":"execute"},
        {"op":"connect_pins","graph":"EventGraph","from":"clear_event","from_pin":"then","to":"clear","to_pin":"execute"},
        {"op":"connect_pins","graph":"EventGraph","from":"emit_event","from_pin":"then","to":"emit","to_pin":"execute"},
        {"op":"connect_pins","graph":"EventGraph","from":"emit_event","from_pin":"Value","to":"emit","to_pin":"Value"},
        {"op":"connect_pins","graph":"EventGraph","from":"listener","from_pin":"then","to":"store","to_pin":"execute"},
        {"op":"connect_pins","graph":"EventGraph","from":"count","from_pin":"Count","to":"add","to_pin":"A"},
        {"op":"connect_pins","graph":"EventGraph","from":"listener","from_pin":"Value","to":"add","to_pin":"B"},
        {"op":"connect_pins","graph":"EventGraph","from":"add","from_pin":"ReturnValue","to":"store","to_pin":"Count"}
    ])")), Plan));
    return Plan;
}

void Invoke(UObject* Object, FName Name, int32 Value = 0)
{
    UFunction* Function = Object->FindFunction(Name);
    check(Function);
    Object->ProcessEvent(Function, Function->NumParms ? &Value : nullptr);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDelegateRuntime,
    "Nexus.Blueprint.Delegates.Runtime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDelegateRuntime::RunTest(const FString& Parameters)
{
    auto* Blueprint = DelegateBlueprint();
    FApplyContext Context;
    ApplyBlueprintPlan(Blueprint, DelegatePlan(), Context);
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    if (!TestEqual(TEXT("native graph apply"), Context.Failures.Num(), 0))
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (!TestTrue(TEXT("four delegate nodes compile"), Blueprint->Status != BS_Error))
    {
        return false;
    }
    auto* Instance = NewObject<UObject>(GetTransientPackage(), Blueprint->GeneratedClass);
    auto* Count = FindFProperty<FIntProperty>(Instance->GetClass(), TEXT("Count"));
    if (!TestNotNull(TEXT("native listener count"), Count))
    {
        return false;
    }
    Invoke(Instance, TEXT("Bind"));
    Invoke(Instance, TEXT("Emit"), 7);
    TestEqual(TEXT("binding dispatches an actual Blueprint event"), Count->GetPropertyValue_InContainer(Instance), 7);
    Invoke(Instance, TEXT("Bind"));
    Invoke(Instance, TEXT("Emit"), 3);
    TestEqual(TEXT("duplicate binding retains native AddUnique semantics"), Count->GetPropertyValue_InContainer(Instance), 10);
    Invoke(Instance, TEXT("Remove"));
    Invoke(Instance, TEXT("Emit"), 11);
    TestEqual(TEXT("remove stops the bound event"), Count->GetPropertyValue_InContainer(Instance), 10);
    Invoke(Instance, TEXT("Bind"));
    Invoke(Instance, TEXT("Clear"));
    Invoke(Instance, TEXT("Emit"), 13);
    TestEqual(TEXT("clear releases every bound event"), Count->GetPropertyValue_InContainer(Instance), 10);

    UEdGraph* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    for (const FString Id : {TEXT("bind"), TEXT("remove"), TEXT("clear"), TEXT("emit")})
    {
        auto* Node = ResolveGraphNode(Blueprint, Graph, Context, Id);
        const auto Config = NodeConfigJson(Blueprint, Node);
        TestTrue(TEXT("delegate node exports supported text"), IsSupportedNodeClass(Node->GetClass()));
        TestEqual(TEXT("dispatcher member survives export"), Config->GetStringField(TEXT("delegate_name")), FString(TEXT("Changed")));
        TestEqual(TEXT("self binding survives export"), Config->GetStringField(TEXT("self_context")), FString(TEXT("true")));
    }
    const auto Schema = BuildDelegateSchema();
    const TSharedPtr<FJsonObject>* Record = nullptr;
    const FString Key = Blueprint->GeneratedClass->GetPathName() + TEXT(".Changed");
    if (TestTrue(TEXT("multicast signature exported to its own catalog family"), Schema->TryGetObjectField(Key, Record)))
    {
        TestEqual(TEXT("signature belongs to the dispatcher property"), (*Record)->GetStringField(TEXT("path")), Key);
        TestTrue(TEXT("native assignability exported"), (*Record)->GetBoolField(TEXT("assignable")));
        TestEqual(TEXT("signature contains the actual Value parameter"), (*Record)->GetArrayField(TEXT("params")).Num(), 1);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusDelegatePreview,
    "Nexus.Blueprint.Delegates.Preview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusDelegatePreview::RunTest(const FString& Parameters)
{
    auto* Blueprint = DelegateBlueprint();
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, TEXT("EventGraph"));
    const int32 Before = Graph->Nodes.Num();
    FApplyContext Context;
    Context.bDryRun = true;
    ApplyBlueprintPlan(Blueprint, DelegatePlan(), Context);
    TestEqual(TEXT("new dispatcher references preview before reflection"), Context.Failures.Num(), 0);
    TestEqual(TEXT("preview creates no graph nodes"), Graph->Nodes.Num(), Before);
    TestEqual(TEXT("preview creates no dispatcher signature graphs"), Blueprint->DelegateSignatureGraphs.Num(), 0);

    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("delegate_owner"), TEXT("self"));
    Payload->SetStringField(TEXT("delegate_name"), TEXT("Missing"));
    FString Error;
    UClass* Class = UK2Node_CallDelegate::StaticClass();
    const FName BeforeName = Class->GetDefaultObject<UK2Node_BaseMCDelegate>()->GetPropertyName();
    TestFalse(TEXT("missing dispatcher rejected before native node creation"),
        ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
    TestTrue(TEXT("failure identifies missing member"), Error.Contains(TEXT("Missing")));
    TestEqual(TEXT("validation never mutates the node CDO"),
        Class->GetDefaultObject<UK2Node_BaseMCDelegate>()->GetPropertyName(), BeforeName);
    Payload->SetStringField(TEXT("delegate_owner"), TEXT("/Script/CoreUObject.Object"));
    TestFalse(TEXT("external missing member rejected by same typed validator"),
        ValidateBlueprintNodeCreateConfig(Class, Blueprint, Graph, Payload, Error));
    return true;
}

#endif
