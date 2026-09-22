#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusCollaboration.h"
#include "Apply/NexusApplyAssetCreate.h"
#include "Apply/UeNodeNexusBridgeTranscodeApplyResult.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"

#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;

static void ApplyPlanForKind(UObject* Asset, const FString& Kind, const TArray<TSharedPtr<FJsonValue>>& Plan, FApplyContext& Context)
{
    if (Kind == TEXT("material") || Kind == TEXT("material_function"))
    {
        ApplyMaterialPlan(Asset, Plan, Context);
    }
    else if (Kind == TEXT("material_instance"))
    {
        ApplyMaterialInstancePlan(Cast<UMaterialInstanceConstant>(Asset), Plan, Context);
    }
    else if (Kind == TEXT("blueprint"))
    {
        ApplyBlueprintPlan(Cast<UBlueprint>(Asset), Plan, Context);
    }
    else
    {
        ApplyGenericPlan(Asset, Plan, Context);
    }
}

static TSharedPtr<FJsonObject> ApplyAsset(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDelete = false;
    if (Payload->TryGetBoolField(TEXT("delete_asset"), bDelete) && bDelete)
    {
        return Collaboration::DeleteAsset(Operation, RequestId, Payload);
    }
    FString AssetPath;
    FString Kind;
    const TArray<TSharedPtr<FJsonValue>>* Plan = nullptr;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetStringField(TEXT("kind"), Kind) || !Payload->TryGetArrayField(TEXT("plan"), Plan) || Plan == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path, kind and plan (array) are required"));
    }
    bool bDryRun = true;
    bool bCompile = true;
    bool bSave = true;
    bool bCreate = false;
    FString OutDir;
    FString AssetClass;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile"), bCompile);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    Payload->TryGetBoolField(TEXT("create"), bCreate);
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    Payload->TryGetStringField(TEXT("asset_class"), AssetClass);
    if (!OutDir.IsEmpty() && GetMirrorRoot().IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("root_not_set"), TEXT("call transcode_root_set before applying with out_dir"));
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (Kind == TEXT("material_instance"))
    {
        FString ParentError;
        if (!EnsureMaterialParentReady(Asset, *Plan, !bDryRun, ParentError))
        {
            return MakeOperationError(Operation, RequestId, TEXT("parent_material_not_ready"), ParentError);
        }
    }
    bool bCreated = false;
    if (Asset == nullptr)
    {
        if (!bCreate)
        {
            return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("asset could not be loaded: %s"), *AssetPath));
        }
        FString Error;
        if (bDryRun)
        {
            FString PackageName;
            FString AssetName;
            if (!CheckAssetForKind(AssetPath, Kind, *Plan, AssetClass, PackageName, AssetName, Error))
            {
                return MakeOperationError(Operation, RequestId, TEXT("create_failed"), Error);
            }
            TSharedPtr<FJsonObject> DryData = MakeShared<FJsonObject>();
            DryData->SetBoolField(TEXT("dry_run"), true);
            DryData->SetBoolField(TEXT("would_create"), true);
            DryData->SetNumberField(TEXT("applied"), 0);
            TSharedPtr<FJsonObject> DryResponse = MakeEnvelope(Operation, RequestId, true);
            DryResponse->SetObjectField(TEXT("data"), DryData);
            return DryResponse;
        }
        Asset = CreateAssetForKind(AssetPath, Kind, *Plan, AssetClass, Error);
        if (Asset == nullptr)
        {
            return MakeOperationError(Operation, RequestId, TEXT("create_failed"), Error);
        }
        bCreated = true;
    }
    const FString ActualKind = KindForClass(Asset->GetClass());
    if (ActualKind != Kind)
    {
        return MakeOperationError(Operation, RequestId, TEXT("kind_mismatch"), FString::Printf(TEXT("asset is %s, plan is for %s"), *ActualKind, *Kind));
    }

    FApplyContext Context;
    Context.bDryRun = bDryRun;
    const TSharedPtr<FJsonObject>* Ids = nullptr;
    if (Payload->TryGetObjectField(TEXT("ids"), Ids) && Ids != nullptr)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Ids)->Values)
        {
            FString Guid;
            if (Pair.Value.IsValid() && Pair.Value->TryGetString(Guid))
            {
                Context.Ids.Add(Pair.Key, Guid);
            }
        }
    }

    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(FString::Printf(TEXT("UE Node Nexus sync push: %s"), *Asset->GetName())));
        Asset->Modify();
    }
    ApplyPlanForKind(Asset, Kind, *Plan, Context);
    if (Transaction.IsValid() && !Context.Failures.IsEmpty())
    {
        // Failed publication is restored from its package checkpoint. Do not let
        // this request's undo record keep the package loaded during that restore.
        Transaction->Cancel();
    }
    Transaction.Reset();

    FBridgeAssetCompileDiagnostics Compile;
    if (!bDryRun && bCompile)
    {
        Compile = CollectAssetCompileDiagnostics(Asset, AssetPath, true);
    }
    if (!bDryRun && (Context.bChanged || bCreated))
    {
        Asset->MarkPackageDirty();
    }
    const bool bSaveRequired =
        !bDryRun && bSave
        && (Context.bChanged || bCreated || Asset->GetOutermost()->IsDirty());
    bool bSaved = false;
    FString SaveError;
    FString SaveCode;
    if (bSaveRequired && Context.Failures.IsEmpty() && (!Compile.bRan || Compile.bOk))
    {
        bSaved = SavePackageDirect(
            Asset->GetOutermost(), Asset, SaveError, &SaveCode);
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("created"), bCreated);
    Data->SetNumberField(TEXT("applied"), Plan->Num() - Context.Failures.Num());
    Data->SetBoolField(TEXT("changed"), Context.bChanged);
    TArray<TSharedPtr<FJsonValue>> Failed;
    for (const FApplyFailure& Failure : Context.Failures)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("index"), Failure.Index);
        Item->SetStringField(TEXT("code"), Failure.Code);
        Item->SetStringField(TEXT("message"), Failure.Message);
        Failed.Add(MakeShared<FJsonValueObject>(Item));
    }
    Data->SetArrayField(TEXT("failed"), Failed);
    TArray<TSharedPtr<FJsonValue>> Diagnostics = Compile.Diagnostics;
    for (const FApplyFailure& Note : Context.Notes)
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(
            MakeDiagnostic(TEXT("warning"), Note.Code, Note.Message, AssetPath, TEXT("UeNodeNexusBridge"))));
    }
    Data->SetArrayField(TEXT("diagnostics"), Diagnostics);
    Data->SetObjectField(TEXT("compile"), CompileDiagnosticsJson(Compile, bCompile && !bDryRun));
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetBoolField(TEXT("save_required"), bSaveRequired);
    if (!SaveError.IsEmpty())
    {
        Data->SetStringField(TEXT("save_error"), SaveError);
    }
    TSharedPtr<FJsonObject> IdMap = MakeShared<FJsonObject>();
    for (const TPair<FString, FString>& Pair : Context.Created)
    {
        IdMap->SetStringField(Pair.Key, Pair.Value);
    }
    Data->SetObjectField(TEXT("id_map"), IdMap);

    if (!bDryRun && !OutDir.IsEmpty())
    {
        TSharedPtr<FJsonObject> Raw = BuildRawForAsset(Asset, Kind);
        FString File;
        FString Error;
        if (Raw.IsValid() && ResolveRawFile(OutDir, Asset->GetPathName(), File, Error) && WriteJsonFile(File, Raw, Error))
        {
            Data->SetStringField(TEXT("file"), File);
            Data->SetStringField(TEXT("saved_hash"), Raw->GetStringField(TEXT("saved_hash")));
            Data->SetBoolField(TEXT("dirty"), Raw->GetBoolField(TEXT("dirty")));
        }
        else if (!Error.IsEmpty())
        {
            Data->SetStringField(TEXT("export_error"), Error);
        }
    }
    return MakeApplyResponse(
        Operation, RequestId, Data, Context, Compile, Plan->Num(),
        bSaveRequired, bSaved, SaveError, SaveCode);
}

TSharedPtr<FJsonObject> HandleTranscodeApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return Collaboration::RunCommit(Operation, RequestId, Payload, [&](const Collaboration::FJson& Request)
    {
        return ApplyAsset(Operation, RequestId, Request);
    });
}
}
