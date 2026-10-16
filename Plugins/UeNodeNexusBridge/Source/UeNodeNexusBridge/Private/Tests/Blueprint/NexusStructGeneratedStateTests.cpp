#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/Properties/NexusBlueprintNodeProperties.h"
#include "Blueprint/Properties/NexusNodeGeneratedState.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "K2Node_MakeStruct.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStructGeneratedState,
    "Nexus.Blueprint.StructGeneratedState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStructGeneratedState::RunTest(const FString& Parameters)
{
    UClass* Class = UK2Node_MakeStruct::StaticClass();
    FProperty* Generated = Class->FindPropertyByName(TEXT("ShowPinForProperties"));
    TestNotNull(TEXT("MakeStruct keeps its generated editor array"), Generated);
    TestTrue(TEXT("the editor-maintained pin visibility array is generated state"),
        UeNodeNexusBridge::IsGeneratedNodeProperty(Class, Generated));
    TestFalse(TEXT("the struct type itself is authored state"),
        UeNodeNexusBridge::IsGeneratedNodeProperty(Class, Class->FindPropertyByName(TEXT("StructType"))));

    UK2Node_MakeStruct* Node = NewObject<UK2Node_MakeStruct>(GetTransientPackage());
    const TArray<TSharedPtr<FJsonValue>> Props = UeNodeNexusBridge::Transcode::ExportEditableProps(Node);
    const bool bExported = Props.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Item)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Name;
        return Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr
            && (*Object)->TryGetStringField(TEXT("name"), Name) && Name == TEXT("ShowPinForProperties");
    });
    TestFalse(TEXT("canonical export never carries the generated array"), bExported);

    FString Before;
    FString After;
    FString Error;
    const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueString>(TEXT("()"));
    TestFalse(TEXT("writing the generated array is refused"),
        UeNodeNexusBridge::ApplyBlueprintNodeProperty(Node, TEXT("ShowPinForProperties"), Value, true, false, Before, After, Error));
    TestTrue(TEXT("the refusal names its reason"), Error.Contains(TEXT("node_property_generated")));
    return true;
}

#endif
