#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusArrayLayoutMismatch,
    "Nexus.Issues13.Blueprint.ArrayLayoutMismatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusArrayLayoutMismatch::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusArrayLayout")),
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
        {"op":"bp_variable_add","name":"Objects","type":{"category":"object","subobject":"/Script/Engine.StaticMesh","container":"array"}},
        {"op":"bp_variable_add","name":"Interfaces","type":{"category":"interface","subobject":"/Script/Engine.BlendableInterface","container":"array"}},
        {"op":"create_node","id":"getObjects","class":"K2Node_VariableGet","positional":["Objects"]},
        {"op":"create_node","id":"getInterfaces","class":"K2Node_VariableGet","positional":["Interfaces"]},
        {"op":"create_node","id":"setObjects","class":"K2Node_VariableSet","positional":["Objects"]},
        {"op":"create_node","id":"setInterfaces","class":"K2Node_VariableSet","positional":["Interfaces"]},
        {"op":"connect_pins","from":"getObjects","from_pin":"Objects","to":"setInterfaces","to_pin":"Interfaces"},
        {"op":"connect_pins","from":"getInterfaces","from_pin":"Interfaces","to":"setObjects","to_pin":"Objects"}
    ])"));
    if (!TestEqual(TEXT("both cross-layout connections fail"), Context.Failures.Num(), 2))
    {
        for (const auto& Failure : Context.Failures)
        {
            AddError(Failure.Message);
        }
        return false;
    }
    for (const auto& Failure : Context.Failures)
    {
        TestEqual(TEXT("mismatch code is array_layout_mismatch"), Failure.Code, FString(TEXT("array_layout_mismatch")));
    }
    Apply(TEXT(R"([
        {"op":"connect_pins","from":"getObjects","from_pin":"Objects","to":"setObjects","to_pin":"Objects"},
        {"op":"connect_pins","from":"getInterfaces","from_pin":"Interfaces","to":"setInterfaces","to_pin":"Interfaces"}
    ])"));
    TestEqual(TEXT("matching container layouts still connect"), Context.Failures.Num(), 2);
    return true;
}

#endif
