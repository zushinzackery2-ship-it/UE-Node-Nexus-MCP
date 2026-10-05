#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_SetFieldsInStruct.h"
#include "Transcode/UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "Transcode/UeNodeNexusBridgeTranscodeBlueprintShared.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStructPositional,
    "Nexus.Blueprint.StructPositional",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStructPositional::RunTest(const FString& Parameters)
{
    const FString StructPath = TEXT("/Script/CoreUObject.IntVector4");
    const TArray<FString> Positional =
    {
        StructPath
    };
    UClass* Classes[] =
    {
        UK2Node_MakeStruct::StaticClass(),
        UK2Node_BreakStruct::StaticClass(),
        UK2Node_SetFieldsInStruct::StaticClass()
    };
    for (UClass* Class : Classes)
    {
        const auto Config = MakeShared<FJsonObject>();
        FString Error;
        TestTrue(*FString::Printf(TEXT("%s accepts struct positional argument"), *Class->GetName()),
            UeNodeNexusBridge::Transcode::ConfigureFromPositional(nullptr, Class, Positional, Config, Error));
        FString Actual;
        Config->TryGetStringField(TEXT("struct_type"), Actual);
        TestEqual(*FString::Printf(TEXT("%s maps the struct type before the inherited variable branch"), *Class->GetName()),
            Actual, StructPath);
        TestFalse(TEXT("struct type does not become a variable member"), Config->HasField(TEXT("variable_name")));
        auto* Node = NewObject<UK2Node_StructOperation>(GetTransientPackage(), Class);
        Node->StructType = LoadObject<UScriptStruct>(nullptr, *StructPath);
        TestNotNull(TEXT("native IntVector4 struct is available"), Node->StructType.Get());
        const auto Exported = UeNodeNexusBridge::Transcode::NodeConfigJson(nullptr, Node);
        FString ExportedType;
        Exported->TryGetStringField(TEXT("struct_type"), ExportedType);
        TestEqual(TEXT("native struct path survives readback"), ExportedType, StructPath);
        TestFalse(TEXT("readback does not classify the struct as a variable"), Exported->HasField(TEXT("variable_name")));
    }
    return true;
}

#endif
