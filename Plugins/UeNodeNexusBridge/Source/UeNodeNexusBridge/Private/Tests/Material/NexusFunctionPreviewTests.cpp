#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "Tests/Material/Preview/NexusPreviewRender.h"

namespace
{
template <typename T>
T* AddExpression(UMaterialFunction* Function)
{
    return CastChecked<T>(UMaterialEditingLibrary::CreateMaterialExpressionInFunction(
        Function, T::StaticClass()));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFunctionPreviewLifetime,
    "Nexus.Issues8.Material.FunctionPreviewLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusFunctionPreviewLifetime::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    TStrongObjectPtr<UMaterialFunction> Child(NewObject<UMaterialFunction>(GetTransientPackage()));
    TStrongObjectPtr<UMaterialFunction> Parent(NewObject<UMaterialFunction>(GetTransientPackage()));
    auto* Value = AddExpression<UMaterialExpressionConstant>(Child.Get());
    Value->R = 0.25f;
    auto* ChildOutput = AddExpression<UMaterialExpressionFunctionOutput>(Child.Get());
    ChildOutput->A.Connect(0, Value);
    auto* Call = AddExpression<UMaterialExpressionMaterialFunctionCall>(Parent.Get());
    Call->SetMaterialFunction(Child.Get());
    auto* ParentOutput = AddExpression<UMaterialExpressionFunctionOutput>(Parent.Get());
    ParentOutput->A.Connect(0, Call);
    auto* Removed = AddExpression<UMaterialExpressionConstant>(Parent.Get());
    TWeakObjectPtr<UMaterialExpression> RemovedWeak(Removed);
    UMaterial* Preview = CastChecked<UMaterial>(Parent->GetPreviewMaterial());
    TestEqual(TEXT("Initial preview matches parent collection"), Preview->GetExpressions().Num(), 3);
    TestTrue(TEXT("Initial preview shader is ready"), MaterialResourceStatus(Preview, true).bOk);
    VerifyPreviewPixels(*this, Preview, 0.25f);

    UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Parent.Get(), Removed);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Deleted expression is collected"), RemovedWeak.IsValid());
    int32 NullCount = 0;
    for (UMaterialExpression* Expression : Preview->GetExpressions())
    {
        NullCount += Expression == nullptr ? 1 : 0;
    }
    UE_LOG(LogTemp, Display, TEXT("Before child update: parent=%d preview=%d preview_nulls=%d"),
        Parent->GetExpressions().Num(), Preview->GetExpressions().Num(), NullCount);
    TestEqual(TEXT("Native cached preview retains the collected slot"), NullCount, 1);

    Value->R = 0.75f;
    const FBridgeAssetCompileDiagnostics Result = CollectAssetCompileDiagnostics(
        Child.Get(), Child->GetPathName(), false);
    TestTrue(TEXT("Child compile completes with a live shader and render fence"), Result.bOk);
    TestEqual(TEXT("Parent preview receives the current collection"),
        Preview->GetExpressions().Num(), Parent->GetExpressions().Num());
    TestEqual(TEXT("Parent preview reconnects current output"),
        Preview->GetExpressionInputForProperty(MP_EmissiveColor)->Expression,
        static_cast<UMaterialExpression*>(ParentOutput));
    for (UMaterialExpression* Expression : Preview->GetExpressions())
    {
        TestNotNull(TEXT("Every preview expression remains valid after collection"), Expression);
    }
    TestTrue(TEXT("Dependency update recompiles the parent shader"), MaterialResourceStatus(Preview, true).bOk);
    VerifyPreviewPixels(*this, Preview, 0.75f);
    const auto ParentResult = CollectAssetCompileDiagnostics(Parent.Get(), Parent->GetPathName(), false);
    TestTrue(TEXT("Parent preview compiles after its own update"), ParentResult.bOk);
    VerifyPreviewPixels(*this, Preview, 0.75f);

    TWeakObjectPtr<UMaterialExpression> OldOutput(ParentOutput);
    UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(Parent.Get(), ParentOutput);
    auto* NewOutput = AddExpression<UMaterialExpressionFunctionOutput>(Parent.Get());
    auto* ReplacementValue = AddExpression<UMaterialExpressionConstant>(Parent.Get());
    ReplacementValue->R = 0.125f;
    NewOutput->A.Connect(0, ReplacementValue);
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Old function output is collected"), OldOutput.IsValid());
    const auto Replaced = CollectAssetCompileDiagnostics(Parent.Get(), Parent->GetPathName(), false);
    TestTrue(TEXT("Replacing an output compiles the current graph"), Replaced.bOk);
    TestEqual(TEXT("Replacing an output reconnects the preview root"),
        Preview->GetExpressionInputForProperty(MP_EmissiveColor)->Expression,
        static_cast<UMaterialExpression*>(NewOutput));
    VerifyPreviewPixels(*this, Preview, 0.125f);
    return true;
}
#endif
