#include "Patch/UeNodeNexusBridgeMaterialPatchOps.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialFunctionBuildSpecNormalize.h"
#include "Patch/UeNodeNexusBridgeMaterialFunctionPatchContext.h"
#include "Patch/UeNodeNexusBridgeMaterialFunctionPatchShared.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "UeNodeNexusMaterialPatchPreparation.h"
#include "UObject/StrongObjectPtr.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMaterialFunctionGraphPatch(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
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
    AppendMaterialFunctionBuildSpecOperations(Payload, NormalizedOperations, Diagnostics, Function);
    if (NormalizedOperations.Num() == 0 && !Payload->HasField(TEXT("operations")) && !Payload->HasField(TEXT("nodes")) && !Payload->HasField(TEXT("links")) && !Payload->HasField(TEXT("material_outputs")))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("operations or graph build fields are required")));
        return Response;
    }

    UMaterialFunction* Original = Function;
    TStrongObjectPtr<UMaterialFunction> Preview;
    const bool bPrepared = Diagnostics.IsEmpty() && ValidateMaterialPatchAliases(Function, NormalizedOperations, Diagnostics);
    if (bDryRun && bPrepared)
    {
        Preview.Reset(Cast<UMaterialFunction>(MakeMaterialPatchPreview(Function)));
        if (!Preview.IsValid())
        {
            auto Response = MakeEnvelope(Operation, RequestId, false);
            Response->SetObjectField(TEXT("error"), MakeError(TEXT("preview_failed"), TEXT("cannot duplicate material function graph")));
            return Response;
        }
        Function = Preview.Get();
    }
    bool bChanged = false;
    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun && bPrepared)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Material Function Patch")));
        Function->Modify();
    }

    FMaterialFunctionPatchContext Context;
    int32 Completed = 0;
    for (const TSharedPtr<FJsonValue>& Value : NormalizedOperations)
    {
        if (!bPrepared)
        {
            break;
        }
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyMaterialFunctionPatchOperation(Function, Op, false, Diff, Diagnostics, Context))
        {
            if (Diagnostics.Num() == 0)
            {
                AddFunctionPatchDiagnostic(Diagnostics, TEXT("patch_operation_failed"), TEXT("Material function patch operation failed validation or application"), Function);
            }
            bChanged |= MaterialPatchHasChanges(Diff);
            break;
        }
        bChanged = true;
        ++Completed;
    }

    TSharedPtr<FJsonObject> Compile = CompileAssetWrite(Function, bCompileAfter, !bDryRun && bChanged && bCompileAfter, Diagnostics);
    if (!bDryRun && bChanged)
    {
        Function->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> PinIntegrity = BuildMaterialFunctionPinIntegrity(Function);
    const bool bOk = Diagnostics.Num() == 0 && PinIntegrity->GetBoolField(TEXT("ok"));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    auto Data = MakeWriteDataWithDiffFormat(bDryRun, !bDryRun && bChanged, bChanged, Diff, PinIntegrity, Compile, MakeDirtyState(Original), DiffFormat);
    Data->SetBoolField(TEXT("partial"), !bDryRun && bChanged && !bOk);
    Data->SetNumberField(TEXT("completed_operations"), Completed);
    Data->SetNumberField(TEXT("operation_count"), NormalizedOperations.Num());
    Response->SetObjectField(TEXT("data"), Data);
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}

TSharedPtr<FJsonObject> HandleMaterialFunctionGraphBuild(const FString& Operation, const FString& RequestId, UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleMaterialFunctionGraphPatch(Operation, RequestId, Function, Payload);
}
}
