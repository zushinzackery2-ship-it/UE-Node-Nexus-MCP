#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialFunction.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transcode/UeNodeNexusBridgeTranscodeMaterialApply.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
TArray<TSharedPtr<FJsonValue>> PreviewPlan(const FString& Text)
{
    TArray<TSharedPtr<FJsonValue>> Plan;
    check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Plan));
    return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusTranscodePreviewIsolation,
    "Nexus.Issues10.Material.TranscodePreviewIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusTranscodePreviewIsolation::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    using namespace Transcode;
    const bool OwnerKinds[]
    {
        false, true
    };
    for (bool bFunction : OwnerKinds)
    {
        TStrongObjectPtr<UObject> Owner(bFunction
            ? static_cast<UObject*>(NewObject<UMaterialFunction>())
            : static_cast<UObject*>(NewObject<UMaterial>()));
        auto* Constant = CastChecked<UMaterialExpressionConstant>(CreateNexusMaterialExpression(
            Owner.Get(), UMaterialExpressionConstant::StaticClass(), 10, 20));
        Constant->R = 0.25f;
        const FString Key = MaterialExpressionKey(Constant, OwnerExpressions(Owner.Get()));
        FApplyContext Context;
        Context.bDryRun = true;
        Context.Ids.Add(TEXT("existing"), Key);
        const auto Plan = PreviewPlan(TEXT(R"([
            {"op":"set_node_param","id":"existing","name":"R","value":"0.75"},
            {"op":"set_node_position","id":"existing","x":100,"y":200},
            {"op":"delete_node","id":"existing"}
        ])"));
        ApplyMaterialPlan(Owner.Get(), Plan, Context);
        TestEqual(TEXT("preview executes all three existing-node verbs"), Context.Failures.Num(), 0);
        TestTrue(TEXT("original expression remains in graph"), OwnerExpressions(Owner.Get()).Contains(Constant));
        TestEqual(TEXT("original scalar is unchanged"), Constant->R, 0.25f);
        TestEqual(TEXT("original X is unchanged"), Constant->MaterialExpressionEditorX, 10);
        TestEqual(TEXT("original Y is unchanged"), Constant->MaterialExpressionEditorY, 20);
        TestTrue(TEXT("request context retains dry-run identity"), Context.bDryRun);

        FApplyContext Invalid;
        Invalid.bDryRun = true;
        ApplyMaterialPlan(Owner.Get(), PreviewPlan(TEXT(R"([
            {"op":"create_node","id":"new","class":"Constant"},
            {"op":"set_node_param","id":"new","name":"UnknownNexusProperty","value":"1"}
        ])")), Invalid);
        TestEqual(TEXT("new-node parameter is validated inside the complete preview"), Invalid.Failures.Num(), 1);
        TestEqual(TEXT("failed preview adds no original node"), OwnerExpressions(Owner.Get()).Num(), 1);
        if (!Invalid.Failures.IsEmpty())
        {
            TestEqual(TEXT("intra-plan invalid property has a concrete error"), Invalid.Failures[0].Code, FString(TEXT("param_failed")));
        }

        FApplyContext Deleted;
        Deleted.bDryRun = true;
        Deleted.Ids.Add(TEXT("existing"), Key);
        ApplyMaterialPlan(Owner.Get(), PreviewPlan(TEXT(R"([
            {"op":"delete_node","id":"existing"},
            {"op":"set_node_param","id":"existing","name":"R","value":"0.5"}
        ])")), Deleted);
        TestEqual(TEXT("later operations cannot refer to a deleted preview node"), Deleted.Failures.Num(), 1);
        TestTrue(TEXT("failed delete plan leaves original identity"), OwnerExpressions(Owner.Get()).Contains(Constant));
    }
    return true;
}
#endif
