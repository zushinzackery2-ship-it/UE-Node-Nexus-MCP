#include "UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialBuildSpecNormalize.h"
#include "UeNodeNexusBridgeMaterialPatchApplyOps.h"
#include "UeNodeNexusBridgeMaterialPatchContext.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
bool IsMaterialPatchAsset(UObject* Asset)
{
    return Cast<UMaterial>(Asset) != nullptr;
}

static void AddMaterialPatchDiagnostic(TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FString& Code, const FString& Message, UMaterial* Material)
{
    Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), Code, Message, Material ? Material->GetPathName() : FString(), TEXT("UeNodeNexusBridge"))));
}

TSharedPtr<FJsonObject> HandleMaterialGraphPatch(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDryRun = true;
    bool bCompileAfter = true;
    FString DiffFormat = TEXT("compact");
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile_after"), bCompileAfter);
    Payload->TryGetStringField(TEXT("format"), DiffFormat);

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    TArray<TSharedPtr<FJsonValue>> NormalizedOperations;
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (Payload->HasField(TEXT("operations")) && (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations must be an array")));
        return Response;
    }
    if (Operations != nullptr)
    {
        NormalizedOperations.Append(*Operations);
    }
    AppendMaterialBuildSpecOperations(Payload, NormalizedOperations, Diagnostics, Material);
    if (NormalizedOperations.Num() == 0 && !Payload->HasField(TEXT("operations")) && !Payload->HasField(TEXT("nodes")) && !Payload->HasField(TEXT("links")) && !Payload->HasField(TEXT("material_outputs")))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations or graph build fields are required")));
        return Response;
    }

    bool bChanged = false;
    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Patch")));
        Material->Modify();
    }

    FMaterialPatchContext Context;
    for (const TSharedPtr<FJsonValue>& Value : NormalizedOperations)
    {
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyMaterialPatchOperation(Material, Op, bDryRun, Diff, Diagnostics, Context))
        {
            if (Diagnostics.Num() == 0)
            {
                AddMaterialPatchDiagnostic(Diagnostics, TEXT("patch_operation_failed"), TEXT("Material patch operation failed validation or application"), Material);
            }
            continue;
        }
        bChanged = true;
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, !bDryRun && bCompileAfter, true, 0, 0);
    if (!bDryRun && bChanged && bCompileAfter)
    {
        UMaterialEditingLibrary::RecompileMaterial(Material);
    }
    else if (!bDryRun && bChanged)
    {
        Material->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> PinIntegrity = BuildMaterialPinIntegrity(Material);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), MakeWriteDataWithDiffFormat(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Material), DiffFormat));
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialGraphBuild(const FString& Operation, const FString& RequestId, UMaterial* Material, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleMaterialGraphPatch(Operation, RequestId, Material, Payload);
}
}
