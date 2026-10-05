#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusControlFlowExportTest,
    "UeNodeNexus.Material.ControlFlowRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusControlFlowExportTest::RunTest(const FString& Parameters)
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
    Begin->Exec.Connect(Branch);
    Branch->Then.Connect(End);
    Branch->Else.Connect(End);

    const auto Graph = BuildMaterialRaw(Material)->GetObjectField(TEXT("graph"));
    TestEqual(TEXT("Preserve one value wire and three execution wires"),
        Graph->GetArrayField(TEXT("links")).Num(), 4);
    for (const auto& Item : Graph->GetArrayField(TEXT("nodes")))
    {
        const auto Node = Item->AsObject();
        if (Node->GetStringField(TEXT("class")) == Branch->GetClass()->GetPathName())
        {
            TestEqual(TEXT("Preserve value and execution input pins"),
                Node->GetArrayField(TEXT("inputs")).Num(), 2);
            TestEqual(TEXT("Preserve both execution output pins"),
                Node->GetArrayField(TEXT("outputs")).Num(), 2);
        }
    }
    return true;
}
