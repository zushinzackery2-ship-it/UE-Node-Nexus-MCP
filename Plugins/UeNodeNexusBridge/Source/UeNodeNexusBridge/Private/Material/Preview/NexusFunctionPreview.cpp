#include "Material/Preview/NexusFunctionPreview.h"

#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialFunction.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge
{
namespace
{
bool UsesFunction(UMaterialFunction* Candidate, UMaterialFunction* Changed)
{
    if (Candidate == Changed)
    {
        return true;
    }
    TArray<UMaterialFunctionInterface*> Dependencies;
    Candidate->GetDependentFunctions(Dependencies);
    return Dependencies.Contains(Changed);
}

void SynchronizePreview(UMaterialFunction* Function)
{
    UMaterial* Preview = Function->PreviewMaterial;
    const int32 PreviousCount = Preview->GetExpressions().Num();
    Preview->CancelOutstandingCompilation();
    Preview->AssignExpressionCollection(Function->GetExpressionCollection());
    // Cached previews are derived from the function's current graph, including its roots.
    const EMaterialProperty Properties[]
    {
        MP_EmissiveColor, MP_MaterialAttributes, MP_FrontMaterial
    };
    for (EMaterialProperty Property : Properties)
    {
        if (FExpressionInput* Input = Preview->GetExpressionInputForProperty(Property))
        {
            *Input = FExpressionInput();
        }
    }
    Preview->bUseMaterialAttributes = false;
    Preview->SetShadingModel(MSM_Unlit);
    int32 OutputCount = 0;
    for (UMaterialExpression* Expression : Function->GetExpressions())
    {
        if (auto* Output = Cast<UMaterialExpressionFunctionOutput>(Expression))
        {
            // Match UMaterialFunction::GetPreviewMaterial's native output selection.
            Output->ConnectToPreviewMaterial(Preview, 0);
            ++OutputCount;
        }
    }
    Preview->UpdateCachedExpressionData();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=function_preview_sync asset=%s before=%d after=%d outputs=%d"),
        *ActiveBridgeRequestId(), *Function->GetPathName(), PreviousCount,
        Preview->GetExpressions().Num(), OutputCount);
}
}

UMaterial* UpdateFunctionPreview(UMaterialFunction* Function)
{
    TStrongObjectPtr<UMaterialFunction> Owner(Function);
    TArray<TStrongObjectPtr<UMaterialFunction>> LoadedFunctions;
    for (TObjectIterator<UMaterialFunction> It(RF_ClassDefaultObject, true, EInternalObjectFlags::Garbage); It; ++It)
    {
        // Native deletions also invalidate the engine's function-call candidates.
        // Refresh all candidates before traversing any transitive dependency.
        It->UpdateDependentFunctionCandidates();
        LoadedFunctions.Emplace(*It);
    }
    for (const auto& Candidate : LoadedFunctions)
    {
        if (Candidate->PreviewMaterial && UsesFunction(Candidate.Get(), Function))
        {
            SynchronizePreview(Candidate.Get());
        }
    }
    const bool bHadPreview = Function->PreviewMaterial != nullptr;
    UMaterialEditingLibrary::UpdateMaterialFunction(Function, Function->PreviewMaterial);
    UMaterial* Preview = CastChecked<UMaterial>(Function->GetPreviewMaterial());
    if (bHadPreview)
    {
        // GetPreviewMaterial compiles only on creation; an existing preview needs a new identity.
        Preview->PreEditChange(nullptr);
        Preview->PostEditChange();
    }
    return Preview;
}
}
