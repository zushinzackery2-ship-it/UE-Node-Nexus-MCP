#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Transcode/Blueprint/Members/NexusCustomEventPins.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCustomEventTypes,
    "Nexus.Blueprint.CustomEvent.TypedSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCustomEventTypes::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusEventTypes"));
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(), GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    auto* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"),
        UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"NEXUS([
        {"op":"create_node","graph":"EventGraph","id":"receive","class":"K2Node_CustomEvent",
         "positional":["Receive","Owner: Object(/Script/Engine.Actor)","Items: Array<Object(/Script/Engine.Actor)>","Time: double = 1.25"],
         "custom_event_signature":{"inputs":[
             {"name":"Owner","type":{"category":"object","subobject":"/Script/Engine.Actor"}},
             {"name":"Items","type":{"category":"object","subobject":"/Script/Engine.Actor","container":"array"}},
             {"name":"Time","type":{"category":"real","subcategory":"double"},"default":"1.25"}]}}
    ])NEXUS")), Plan));
    FApplyContext Context;
    ApplyBlueprintPlan(Blueprint, Plan, Context);
    for (const auto& Failure : Context.Failures)
    {
        AddError(Failure.Message);
    }
    if (!TestEqual(TEXT("typed event applies"), Context.Failures.Num(), 0))
    {
        return false;
    }
    auto* Event = Cast<UK2Node_CustomEvent>(ResolveGraphNode(Blueprint, Graph, Context, TEXT("receive")));
    if (!TestNotNull(TEXT("custom event exists"), Event))
    {
        return false;
    }
    const auto* Owner = Event->FindPin(TEXT("Owner"), EGPD_Output);
    const auto* Items = Event->FindPin(TEXT("Items"), EGPD_Output);
    const auto* Time = Event->FindPin(TEXT("Time"), EGPD_Output);
    if (!TestNotNull(TEXT("object parameter"), Owner) || !TestNotNull(TEXT("array parameter"), Items)
        || !TestNotNull(TEXT("double parameter"), Time))
    {
        return false;
    }
    TestEqual(TEXT("object uses native category"), Owner->PinType.PinCategory, UEdGraphSchema_K2::PC_Object);
    TestTrue(TEXT("object resolves its actual class"), Owner->PinType.PinSubCategoryObject.Get() == AActor::StaticClass());
    TestTrue(TEXT("array retains its container"), Items->PinType.ContainerType == EPinContainerType::Array);
    TestEqual(TEXT("double retains precision"), Time->PinType.PinSubCategory, UEdGraphSchema_K2::PC_Double);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("typed event compiles in a native Object Blueprint"), Blueprint->Status != BS_Error);

    auto Bad = MakeShared<FJsonObject>();
    Bad->SetArrayField(TEXT("positional"), {MakeShared<FJsonValueString>(TEXT("Receive")),
        MakeShared<FJsonValueString>(TEXT("Owner: Object(/Script/Engine.Actor)"))});
    FString Error;
    TestFalse(TEXT("unstructured reflected type rejected before mutation"), ConfigureCustomEventPins(Event, Bad, Error));
    TestFalse(TEXT("rejection explains signature requirement"), Error.IsEmpty());
    TestTrue(TEXT("rejected update retains the original pin"), Event->FindPin(TEXT("Owner"), EGPD_Output) == Owner);
    return true;
}

#endif
