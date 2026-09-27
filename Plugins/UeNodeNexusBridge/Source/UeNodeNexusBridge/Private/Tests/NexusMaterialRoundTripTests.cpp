#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/Fixtures/NexusPrecisionFixture.h"
#include "Material/Creation/NexusMaterialExpressionCreate.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "Diagnostics/UeNodeNexusBridgeDiagnostics.h"
#include "UObject/UnrealType.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusPrecisionRoundTrip, "Nexus.Issues2.PropertyRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusPrecisionRoundTrip::RunTest(const FString& Parameters)
{
    UNexusPrecisionFixture* Source = NewObject<UNexusPrecisionFixture>();
    Source->Scalar = 0.338f;
    Source->Double = -1.0e-100;
    Source->Nested.Tiny = 1.0e-30f;
    Source->Nested.Precise = 1.2345678901234567e100;
    Source->Nested.Transform.SetTranslation(FVector(1.0e-12, -1.2345678901234567, 1.0e15));
    Source->Nested.Transform.SetScale3D(FVector(1.0e-8, 0.12345678901234567, 1.0));
    Source->Array.Add(Source->Nested);
    Source->Set.Add(1.0e-100);
    Source->Set.Add(1.2345678901234567);
    Source->Map.Add(TEXT("quoted, key"), Source->Nested);
    UNexusPrecisionFixture* Copy = NewObject<UNexusPrecisionFixture>();
    for (TFieldIterator<FProperty> It(Source->GetClass()); It; ++It)
    {
        const FString Text = ExportPropertyValue(Source, *It);
        FString Error;
        TestTrue(It->GetName() + TEXT(" imports: ") + Text, ImportPropertyValue(Copy, It->GetName(), Text, Error, false));
        TestTrue(It->GetName() + TEXT(" preserves every value"), It->Identical_InContainer(Source, Copy));
    }
    TestEqual(TEXT("string literals retain their exact content"), Copy->Nested.Literal, Source->Nested.Literal);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMaterialCreationDefaults, "Nexus.Issues2.MaterialCreationDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMaterialCreationDefaults::RunTest(const FString& Parameters)
{
    TArray<UObject*> Owners;
    Owners.Add(NewObject<UMaterial>());
    Owners.Add(NewObject<UMaterialFunction>());
    for (UObject* Owner : Owners)
    {
        auto* Mask = CastChecked<UMaterialExpressionComponentMask>(CreateNexusMaterialExpression(Owner, UMaterialExpressionComponentMask::StaticClass(), 0, 0));
        FString Error;
        TestTrue(TEXT("B=True is accepted"), ImportPropertyValue(Mask, TEXT("B"), TEXT("True"), Error, false));
        TestTrue(TEXT("single B channel remains single B"), Mask->B && !Mask->R && !Mask->G && !Mask->A);
        TestEqual(TEXT("readback reports B"), ExportPropertyValue(Mask, FindFProperty<FProperty>(Mask->GetClass(), TEXT("B"))), FString(TEXT("True")));
        auto* Static = CastChecked<UMaterialExpressionStaticComponentMaskParameter>(CreateNexusMaterialExpression(Owner, UMaterialExpressionStaticComponentMaskParameter::StaticClass(), 0, 0));
        TestEqual(TEXT("static mask uses its CDO"), bool(Static->DefaultR), bool(GetDefault<UMaterialExpressionStaticComponentMaskParameter>()->DefaultR));
        auto* Transform = CastChecked<UMaterialExpressionTransformPosition>(CreateNexusMaterialExpression(Owner, UMaterialExpressionTransformPosition::StaticClass(), 0, 0));
        TestEqual(TEXT("position source uses its CDO"), Transform->TransformSourceType, GetDefault<UMaterialExpressionTransformPosition>()->TransformSourceType);
        TestEqual(TEXT("position target uses its CDO"), Transform->TransformType, GetDefault<UMaterialExpressionTransformPosition>()->TransformType);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMaterialCompilationState, "Nexus.Issues2.MaterialCompilationState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMaterialCompilationState::RunTest(const FString& Parameters)
{
    UMaterial* Material = NewObject<UMaterial>();
    auto* Constant = CastChecked<UMaterialExpressionConstant>(CreateNexusMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), 0, 0));
    Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Constant);
    const float Values[] =
    {
        0.25f, 0.5f
    };
    for (float Value : Values)
    {
        const FGuid Before = Material->StateId;
        Constant->R = Value;
        CollectAssetCompileDiagnostics(Material, Material->GetPathName(), false);
        TestTrue(TEXT("each graph edit changes the shader map and DDC identity"), Material->StateId.IsValid() && Material->StateId != Before);
    }
    return true;
}

#endif
