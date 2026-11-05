#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

namespace
{
FApplyContext ApplyInstanceParameter(UMaterialInstanceConstant* Instance, const TCHAR* Kind,
    const TCHAR* Name, const TCHAR* Value, bool bClear = false,
    const TCHAR* Association = TEXT("global"), int32 Index = INDEX_NONE)
{
    auto Op = MakeShared<FJsonObject>();
    Op->SetStringField(TEXT("op"), bClear ? TEXT("mi_clear_param") : TEXT("mi_set_param"));
    Op->SetStringField(TEXT("kind"), Kind);
    Op->SetStringField(TEXT("name"), Name);
    Op->SetStringField(TEXT("value"), Value);
    Op->SetStringField(TEXT("association"), Association);
    Op->SetNumberField(TEXT("index"), Index);
    FApplyContext Context;
    Context.bDryRun = false;
    ApplyMaterialInstancePlan(Instance, { MakeShared<FJsonValueObject>(Op) }, Context);
    return Context;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInstanceParameterValidation,
    "Nexus.Material.InstanceParameterValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInstanceParameterValidation::RunTest(const FString& Parameters)
{
    UMaterial* Parent = NewObject<UMaterial>();
    auto* Scalar = CastChecked<UMaterialExpressionScalarParameter>(CreateNexusMaterialExpression(
        Parent, UMaterialExpressionScalarParameter::StaticClass(), 0, 0));
    Scalar->ParameterName = TEXT("Amount");
    Scalar->DefaultValue = 0.5f;
    auto* Mask = CastChecked<UMaterialExpressionStaticComponentMaskParameter>(CreateNexusMaterialExpression(
        Parent, UMaterialExpressionStaticComponentMaskParameter::StaticClass(), 0, 0));
    Mask->ParameterName = TEXT("Channels");
    Parent->UpdateCachedExpressionData();
    UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>();
    Instance->SetParentEditorOnly(Parent);
    for (const TCHAR* Kind : { TEXT("switch"), TEXT("component_mask") })
    {
        const TCHAR* Value = FString(Kind) == TEXT("switch") ? TEXT("true") : TEXT("(R=True,G=False,B=False,A=False)");
        const auto Context = ApplyInstanceParameter(Instance, Kind, TEXT("Ghost"), Value);
        TestEqual(FString(Kind) + TEXT(" rejects a parameter absent from its parent"), Context.Failures.Num(), 1);
        TestFalse(TEXT("rejection reports no change"), Context.bChanged);
    }
    TestEqual(TEXT("missing switch does not create a dead override"), Instance->GetStaticParameters().StaticSwitchParameters.Num(), 0);
    TestEqual(TEXT("missing mask does not create a dead override"), Instance->GetStaticParameters().EditorOnly.StaticComponentMaskParameters.Num(), 0);
    for (const TCHAR* Text : { TEXT("garbage"), TEXT("."), TEXT("1e+"), TEXT("1e1000") })
    {
        TestEqual(TEXT("invalid scalar is rejected"), ApplyInstanceParameter(Instance, TEXT("scalar"), TEXT("Amount"), Text).Failures.Num(), 1);
        TestEqual(TEXT("invalid scalar does not create an override"), Instance->ScalarParameterValues.Num(), 0);
    }
    TestEqual(TEXT("valid scalar is accepted"), ApplyInstanceParameter(Instance, TEXT("scalar"), TEXT("Amount"), TEXT("0.75")).Failures.Num(), 0);
    for (const TCHAR* Text : { TEXT("(R=True)"), TEXT("(R=True,G=typo,B=False,A=False)"),
        TEXT("(R=True,R=False,B=False,A=False)") })
    {
        TestEqual(TEXT("invalid mask is rejected"), ApplyInstanceParameter(Instance, TEXT("component_mask"), TEXT("Channels"), Text).Failures.Num(), 1);
    }
    const auto ValidMask = ApplyInstanceParameter(Instance, TEXT("component_mask"), TEXT("Channels"), TEXT("(R=1,G=0,B=1,A=0)"));
    TestEqual(TEXT("valid numeric booleans are accepted"), ValidMask.Failures.Num(), 0);
    for (const auto& Failure : ValidMask.Failures)
    {
        AddInfo(Failure.Message);
    }
    const auto Masks = Instance->GetStaticParameters().EditorOnly.StaticComponentMaskParameters;
    if (TestEqual(TEXT("only the parent mask is overridden"), Masks.Num(), 1))
    {
        TestTrue(TEXT("numeric channels are preserved"), Masks[0].R && !Masks[0].G && Masks[0].B && !Masks[0].A);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInstanceLayerIdentity,
    "Nexus.Material.InstanceLayerIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInstanceLayerIdentity::RunTest(const FString& Parameters)
{
    UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>();
    Instance->SetParentEditorOnly(NewObject<UMaterial>());
    for (int32 Index = 0; Index < 2; ++Index)
    {
        FScalarParameterValue Value;
        Value.ParameterInfo = FMaterialParameterInfo(TEXT("Amount"), LayerParameter, Index);
        Value.ParameterValue = float(Index + 1);
        Instance->ScalarParameterValues.Add(Value);
    }
    const auto Rows = BuildMaterialInstanceRaw(Instance)->GetObjectField(TEXT("instance"))->GetArrayField(TEXT("scalar"));
    TestEqual(TEXT("both layer parameters are exported"), Rows.Num(), 2);
    for (int32 Index = 0; Index < Rows.Num(); ++Index)
    {
        FString Association;
        double LayerIndex = -1;
        Rows[Index]->AsObject()->TryGetStringField(TEXT("association"), Association);
        Rows[Index]->AsObject()->TryGetNumberField(TEXT("index"), LayerIndex);
        TestEqual(TEXT("layer association is retained"), Association, FString(TEXT("layer")));
        TestEqual(TEXT("layer index is retained"), LayerIndex, double(Index));
    }
    ApplyInstanceParameter(Instance, TEXT("scalar"), TEXT("Amount"), TEXT(""), true);
    TestEqual(TEXT("clearing a global parameter preserves layers with that name"), Instance->ScalarParameterValues.Num(), 2);
    const auto Cleared = ApplyInstanceParameter(Instance, TEXT("scalar"), TEXT("Amount"), TEXT(""), true, TEXT("layer"), 0);
    TestEqual(TEXT("selected layer clear succeeds"), Cleared.Failures.Num(), 0);
    if (TestEqual(TEXT("selected layer clear retains its sibling"), Instance->ScalarParameterValues.Num(), 1))
    {
        TestEqual(TEXT("remaining override belongs to layer one"), Instance->ScalarParameterValues[0].ParameterInfo.Index, 1);
    }
    return true;
}
#endif
