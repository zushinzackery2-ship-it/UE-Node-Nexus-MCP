#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/Patch/UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Character.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Transcode/Blueprint/Apply/UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "Transcode/Blueprint/UeNodeNexusBridgeTranscodeBlueprintShared.h"

using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNativeFunctionOverride,
    "Nexus.Blueprint.NativeFunctionOverride",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNativeFunctionOverride::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusNativeOverride"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ACharacter::StaticClass(), GetTransientPackage(),
        Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TSharedPtr<FJsonObject> Op;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({
        "name":"CanJumpInternal",
        "signature":{"inputs":[],"outputs":[{"name":"ReturnValue","type":{"category":"bool"}}],
                     "flags":["Protected","Const"]}
    })")), Op);
    FString Error;
    if (!TestTrue(TEXT("protected native event accepts its inherited signature"),
        ApplyFunctionVerb(Blueprint, TEXT("bp_function_add"), Op, Error)))
    {
        AddError(Error);
        return false;
    }
    UEdGraph* Graph = UeNodeNexusBridge::FindBlueprintGraph(Blueprint, TEXT("CanJumpInternal"));
    if (!TestNotNull(TEXT("override graph exists"), Graph))
    {
        return false;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    TArray<UK2Node_FunctionResult*> Results;
    Graph->GetNodesOfClass(Entries);
    Graph->GetNodesOfClass(Results);
    if (!TestEqual(TEXT("one inherited entry"), Entries.Num(), 1)
        || !TestEqual(TEXT("one inherited result"), Results.Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("entry references the native declaring class"),
        Entries[0]->FunctionReference.GetMemberParentClass(), ACharacter::StaticClass());
    TestEqual(TEXT("entry retains native function name"),
        Entries[0]->FunctionReference.GetMemberName(), FName(TEXT("CanJumpInternal")));
    TestFalse(TEXT("override signature cannot be renamed"), Entries[0]->IsEditable());
    TestNotNull(TEXT("native return is not suffixed"), Results[0]->FindPin(TEXT("ReturnValue")));
    TestNull(TEXT("duplicate user return is absent"), Results[0]->FindPin(TEXT("ReturnValue1")));
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("native override compiles without errors"), Log.NumErrors, 0);
    const auto Export = FunctionSignatureJson(Graph);
    const auto& Outputs = Export->GetArrayField(TEXT("outputs"));
    if (TestEqual(TEXT("readback has one output"), Outputs.Num(), 1))
    {
        TestEqual(TEXT("readback retains native return name"),
            Outputs[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("ReturnValue")));
    }

    auto Ordinary = MakeShared<FJsonObject>();
    Ordinary->SetStringField(TEXT("name"), TEXT("CustomQuery"));
    TestTrue(TEXT("ordinary new functions remain supported"),
        ApplyFunctionVerb(Blueprint, TEXT("bp_function_add"), Ordinary, Error));
    auto Forbidden = MakeShared<FJsonObject>();
    Forbidden->SetStringField(TEXT("name"), TEXT("K2_DestroyActor"));
    TestFalse(TEXT("non-event inherited functions reject shadowing"),
        ApplyFunctionVerb(Blueprint, TEXT("bp_function_add"), Forbidden, Error));
    TestTrue(TEXT("rejection names the inherited function"), Error.Contains(TEXT("K2_DestroyActor")));
    return true;
}
#endif
