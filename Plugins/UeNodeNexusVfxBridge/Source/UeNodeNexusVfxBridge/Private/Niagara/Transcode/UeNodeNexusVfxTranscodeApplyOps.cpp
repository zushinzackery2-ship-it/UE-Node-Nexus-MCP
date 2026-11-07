#include "UeNodeNexusVfxTranscode.h"
#include "UeNodeNexusCollaboration.h"
#include "Niagara/Compilation/NexusNiagaraCompilation.h"
#include "Binding/NexusNiagaraInputBinding.h"

#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "Misc/PackageName.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeAssetPaths.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusNiagaraOps.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;
using namespace VfxTranscode;

static TSharedPtr<FJsonObject> ApplySystem(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDelete = false;
    if (Payload->TryGetBoolField(TEXT("delete_asset"), bDelete) && bDelete)
    {
        return Collaboration::DeleteAsset(Operation, RequestId, Payload);
    }
    FString AssetPath;
    const TArray<TSharedPtr<FJsonValue>>* Plan = nullptr;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetArrayField(TEXT("plan"), Plan) || Plan == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path and plan (array) are required"));
    }
    bool bDryRun = true;
    bool bCompile = true;
    bool bSave = true;
    bool bCreate = false;
    FString OutDir;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile"), bCompile);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    Payload->TryGetBoolField(TEXT("create"), bCreate);
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    FString PackageName, AssetName;
    FText PathError;
    if (!ParseAssetPath(AssetPath, PackageName, AssetName, PathError))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_asset_path"), PathError.ToString());
    }
    AssetPath = PackageName + TEXT(".") + AssetName;
    UObject* Asset = FindObject<UObject>(nullptr, *AssetPath);
    if (Asset == nullptr && FPackageName::DoesPackageExist(PackageName))
    {
        Asset = LoadObject<UObject>(nullptr, *AssetPath);
    }
    UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset);
    if (IsValid(Asset) && (!IsValid(System) || !System->IsAsset()))
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_type_mismatch"), TEXT("asset_path must identify a NiagaraSystem asset"));
    }
    bool bCreated = false;
    if (!IsValid(System))
    {
        if (!bCreate)
        {
            return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), TEXT("Niagara system could not be loaded"));
        }
        // Use the same native initialization and ownership as typed asset creation.
        // The surrounding collaboration transaction restores absence on failure.
        auto CreateRequest = MakeShared<FJsonObject>();
        CreateRequest->SetStringField(TEXT("asset_path"), AssetPath);
        CreateRequest->SetBoolField(TEXT("create_default_nodes"), true);
        CreateRequest->SetBoolField(TEXT("dry_run"), bDryRun);
        CreateRequest->SetBoolField(TEXT("save"), false);
        auto Created = HandleNiagaraSystemCreate(Operation, RequestId, CreateRequest);
        if (!Created->GetBoolField(TEXT("ok")))
        {
            return Created;
        }
        if (bDryRun)
        {
            auto Data = Created->GetObjectField(TEXT("data"));
            Data->SetBoolField(TEXT("would_create"), true);
            Data->SetNumberField(TEXT("applied"), 0);
            return Created;
        }
        System = LoadObject<UNiagaraSystem>(nullptr, *Created->GetObjectField(TEXT("data"))->GetStringField(TEXT("asset_path")));
        if (!IsValid(System))
        {
            return MakeOperationError(Operation, RequestId, TEXT("asset_create_failed"), TEXT("created Niagara system could not be resolved"));
        }
        bCreated = true;
        UE_LOG(LogTemp, Display, TEXT("Nexus event=niagara_text_create request=%s asset=%s"), *RequestId, *System->GetPathName());
    }
    FApplyContext Context;
    FInputBindingContext Bindings;
    Context.bDryRun = bDryRun;
    Context.bChanged = bCreated;
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
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(FString::Printf(TEXT("UE Node Nexus sync push: %s"), *System->GetName())));
        System->Modify();
        for (int32 Index = 0; Index < Plan->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Op = (*Plan)[Index].IsValid() ? (*Plan)[Index]->AsObject() : nullptr;
            if (!Op.IsValid())
            {
                Context.Fail(Index, TEXT("invalid_verb"), TEXT("plan entries must be objects"));
                continue;
            }
            ApplyNiagaraVerb(System, Op, Index, Context, Bindings);
        }
    }
    // Release native stack callbacks before the transaction compiles and saves.
    Bindings.ViewModel.Reset();
    bool bReady = true;
    bool bCompileRequested = false;
    bool bCompilationActive = false;
    if (!bDryRun && bCompile)
    {
        NiagaraModuleStack::MarkSystemEdited(System);
        bCompileRequested = NiagaraCompilation::Request(System);
        bCompilationActive = System->HasActiveCompilations();
        bReady = NiagaraCompilation::Wait(System);
    }
    bool bSaved = false;
    FString SaveError;
    const bool bSaveRequired = !bDryRun && bSave
        && (Context.bChanged || System->GetOutermost()->IsDirty());
    if (bSaveRequired && bReady && Context.Failures.IsEmpty())
    {
        bSaved = SavePackageDirect(System, SaveError);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetNumberField(TEXT("applied"), Plan->Num() - Context.Failures.Num());
    Data->SetBoolField(TEXT("changed"), Context.bChanged);
    Data->SetBoolField(TEXT("created"), bCreated);
    Data->SetArrayField(TEXT("input_bindings"), Bindings.Results);
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
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetBoolField(TEXT("ran"), !bDryRun && bCompile);
    Compile->SetBoolField(TEXT("requested"), bCompileRequested);
    Compile->SetBoolField(TEXT("active_after_request"), bCompilationActive);
    Compile->SetBoolField(TEXT("outstanding_after_wait"), System->HasOutstandingCompilationRequests());
    Compile->SetBoolField(TEXT("ok"), bReady);
    NiagaraCompilation::AppendStatus(System, Compile);
    Compile->SetNumberField(TEXT("error_count"), bReady ? 0 : 1);
    if (!bReady)
    {
        Compile->SetArrayField(TEXT("scripts"), NiagaraCompilation::Diagnostics(System));
    }
    Data->SetObjectField(TEXT("compile"), Compile);
    Data->SetArrayField(TEXT("diagnostics"), TArray<TSharedPtr<FJsonValue>>());
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
        FString ReadError;
        TSharedPtr<FJsonObject> Raw = Collaboration::StampRaw(BuildNiagaraSystemRaw(System, &ReadError));
        if (!Raw.IsValid())
        {
            return MakeOperationError(Operation, RequestId, TEXT("niagara_storage_invalid"), ReadError);
        }
        FString File;
        FString Error;
        if (ResolveRawFile(OutDir, System->GetPathName(), File, Error) && WriteJsonFile(File, Raw, Error))
        {
            Data->SetStringField(TEXT("file"), File);
            Data->SetStringField(TEXT("saved_hash"), Raw->GetStringField(TEXT("saved_hash")));
            Data->SetBoolField(TEXT("dirty"), Raw->GetBoolField(TEXT("dirty")));
        }
    }
    const bool bSaveFailed = bSaveRequired && !bSaved;
    Data->SetNumberField(TEXT("remaining_errors"), Context.Failures.Num() + (bReady ? 0 : 1) + (bSaveFailed ? 1 : 0));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, Context.Failures.Num() == 0 && bReady && !bSaveFailed);
    Response->SetObjectField(TEXT("data"), Data);
    if (Context.Failures.Num() > 0)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("plan_partially_failed")), FString::Printf(TEXT("%d of %d plan verbs failed; first: %s"), Context.Failures.Num(), Plan->Num(), *Context.Failures[0].Message)));
    }
    else if (bSaveFailed)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(SaveErrorCode(SaveError), SaveError));
    }
    else if (!bReady)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(
            FString(TEXT("compile_failed")), FString(TEXT("Niagara compile reported errors"))));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleVfxTranscodeApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return Collaboration::RunCommit(Operation, RequestId, Payload, [&](const Collaboration::FJson& Request)
    {
        return ApplySystem(Operation, RequestId, Request);
    });
}
}
