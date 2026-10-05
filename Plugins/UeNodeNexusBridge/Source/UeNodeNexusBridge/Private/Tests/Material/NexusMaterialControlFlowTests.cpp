#include "Misc/AutomationTest.h"
#include "MaterialEditingLibrary.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Material/ControlFlow/NexusMaterialControlFlow.h"
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMaterialControlFlowTest,
    "UeNodeNexus.Material.ControlFlowRootAndLinks",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMaterialControlFlowTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace UeNodeNexusBridge::Transcode;
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
    auto* Begin = CastChecked<UMaterialExpressionExecBegin>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionExecBegin::StaticClass(), 0, 0));
    auto* Branch = CastChecked<UMaterialExpressionIfThenElse>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionIfThenElse::StaticClass(), 200, 0));
    auto* End = CastChecked<UMaterialExpressionExecEnd>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionExecEnd::StaticClass(), 400, 0));
    auto* Value = CastChecked<UMaterialExpressionConstant>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionConstant::StaticClass(), 0, 200));
    Branch->Condition.Connect(0, Value);
    TestEqual(TEXT("Begin root is registered"), Material->GetExpressionExecBegin(), Begin);
    TestEqual(TEXT("End root is registered"), Material->GetExpressionExecEnd(), End);
    TestEqual(TEXT("Begin creation reuses the mandatory root"), CreateNexusMaterialExpression(
        Material, UMaterialExpressionExecBegin::StaticClass(), 0, 0), static_cast<UMaterialExpression*>(Begin));
    TestEqual(TEXT("End creation reuses the mandatory root"), CreateNexusMaterialExpression(
        Material, UMaterialExpressionExecEnd::StaticClass(), 400, 0), static_cast<UMaterialExpression*>(End));
    TestEqual(TEXT("Root acquisition adds no unused expressions"), Material->GetExpressions().Num(), 4);
    FString Error;
    TestTrue(TEXT("Begin execution wire"), ApplyMaterialControlFlowLink(Material, Begin, Branch, TEXT("Exec"), true, Error));
    TestTrue(TEXT("Then execution wire"), ApplyMaterialControlFlowLink(Material, Branch, End, TEXT("Then"), true, Error));
    TestTrue(TEXT("Else execution wire"), ApplyMaterialControlFlowLink(Material, Branch, End, TEXT("Else"), true, Error));
    TArray<TSharedPtr<FJsonValue>> Inputs;
    TArray<TSharedPtr<FJsonValue>> Outputs;
    AppendMaterialControlFlowPins(Branch, Inputs, Outputs);
    TestEqual(TEXT("Execution input name"), Inputs[0]->AsString(), FString(TEXT("execute")));
    TestEqual(TEXT("Branch execution output count"), Outputs.Num(), 2);
    const auto Graph = BuildMaterialRaw(Material)->GetObjectField(TEXT("graph"));
    TestEqual(TEXT("Value wire and three execution wires exported"), Graph->GetArrayField(TEXT("links")).Num(), 4);
    TestFalse(TEXT("Reject value expression as execution target"),
        ApplyMaterialControlFlowLink(Material, Begin, Value, TEXT("Exec"), true, Error));
    TestFalse(TEXT("Reject missing execution output"),
        ApplyMaterialControlFlowLink(Material, Branch, End, TEXT("Missing"), true, Error));
    TestTrue(TEXT("Disconnect incoming execution wire"),
        ApplyMaterialControlFlowLink(Material, nullptr, Branch, TEXT(""), false, Error));
    TestNull(TEXT("Begin output disconnected"), Begin->Exec.GetExpression());
    TestEqual(TEXT("Then output retained"), Branch->Then.GetExpression(), static_cast<UMaterialExpression*>(End));
    Begin->Exec.Connect(Branch);
    TestEqual(TEXT("Two incoming execution wires before deletion"), End->NumExecutionInputs, 2);
    DisconnectMaterialControlFlowExpression(Material, Branch);
    UMaterialEditingLibrary::DeleteMaterialExpression(Material, Branch);
    TestNull(TEXT("Deleting branch clears its incoming execution wire"), Begin->Exec.GetExpression());
    TestEqual(TEXT("Deleting branch clears its outgoing execution counts"), End->NumExecutionInputs, 0);
    return true;
}
