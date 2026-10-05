#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "Transcode/UeNodeNexusBridgeTranscodeMaterialApply.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNumericExecuteTest,
    "UeNodeNexus.Material.ControlFlowNumericExecute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNumericExecuteTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace UeNodeNexusBridge::Transcode;
    UMaterial* Material = NewObject<UMaterial>(GetTransientPackage());
    auto* Value = CastChecked<UMaterialExpressionConstant>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionConstant::StaticClass(), 0, 0));
    auto* Target = CastChecked<UMaterialExpressionCustom>(CreateNexusMaterialExpression(
        Material, UMaterialExpressionCustom::StaticClass(), 200, 0));
    Target->Inputs.Reset();
    Target->Inputs.AddDefaulted_GetRef().InputName = TEXT("execute");
    FApplyContext Context;
    Context.Ids.Add(TEXT("value"), MaterialExpressionKey(Value, Material->GetExpressions()));
    Context.Ids.Add(TEXT("target"), MaterialExpressionKey(Target, Material->GetExpressions()));
    auto Op = MakeShared<FJsonObject>();
    Op->SetStringField(TEXT("from"), TEXT("value"));
    Op->SetStringField(TEXT("from_pin"), TEXT("0"));
    Op->SetStringField(TEXT("to"), TEXT("target"));
    Op->SetStringField(TEXT("to_pin"), TEXT("execute"));
    TestTrue(TEXT("A value input named execute keeps numeric link semantics"),
        ApplyMaterialLink(Material, Op, 0, Context, true));
    TestEqual(TEXT("Numeric execute input receives its value expression"),
        Target->Inputs[0].Input.Expression, static_cast<UMaterialExpression*>(Value));
    TestTrue(TEXT("A value input named execute can disconnect"),
        ApplyMaterialLink(Material, Op, 1, Context, false));
    TestNull(TEXT("Numeric execute input disconnected"), Target->Inputs[0].Input.Expression);
    return true;
}
