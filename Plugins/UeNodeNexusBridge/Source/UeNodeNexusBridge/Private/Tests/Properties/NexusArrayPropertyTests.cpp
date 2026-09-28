#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/MaterialParameterCollection.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusArrayPropertyTests,
    "Nexus.Issues3.Properties.JsonArrays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusArrayPropertyTests::RunTest(const FString& Parameters)
{
    auto* Collection = NewObject<UMaterialParameterCollection>();
    auto* Property = FindFProperty<FProperty>(Collection->GetClass(), TEXT("ScalarParameters"));
    auto Entry = MakeShared<FJsonObject>();
    Entry->SetStringField(TEXT("ParameterName"), TEXT("Amount"));
    Entry->SetNumberField(TEXT("DefaultValue"), 0.7);
    TArray<TSharedPtr<FJsonValue>> Items;
    Items.Add(MakeShared<FJsonValueObject>(Entry));
    Items.Add(MakeShared<FJsonValueObject>(Entry));
    FString Text;
    FString Error;
    TestTrue(TEXT("top-level JSON array is imported"),
        ApplyPropertyJsonValue(Collection, Property, MakeShared<FJsonValueArray>(Items), Text, Error));
    TestEqual(TEXT("element count retained"), Collection->ScalarParameters.Num(), 2);
    if (Collection->ScalarParameters.Num() == 2)
    {
        TestEqual(TEXT("element value retained"), Collection->ScalarParameters[1].DefaultValue, 0.7f);
    }
    Entry->SetStringField(TEXT("DefaultValue"), TEXT("wrong type"));
    TestFalse(TEXT("bad element is rejected"),
        ApplyPropertyJsonValue(Collection, Property, MakeShared<FJsonValueArray>(Items), Text, Error));
    TestEqual(TEXT("failed write preserves count"), Collection->ScalarParameters.Num(), 2);
    Items.Empty();
    TestTrue(TEXT("explicit empty array clears elements"),
        ApplyPropertyJsonValue(Collection, Property, MakeShared<FJsonValueArray>(Items), Text, Error));
    TestEqual(TEXT("empty count"), Collection->ScalarParameters.Num(), 0);
    return true;
}

#endif
