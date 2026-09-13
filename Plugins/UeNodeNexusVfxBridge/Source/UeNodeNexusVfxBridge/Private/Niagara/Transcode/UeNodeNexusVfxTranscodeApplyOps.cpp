#include "UeNodeNexusVfxTranscode.h"
#include "UeNodeNexusCollaboration.h"

#include "NiagaraSystem.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeNiagaraModuleStack.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

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
    FString OutDir;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile"), bCompile);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
    if (System == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), TEXT("Niagara system could not be loaded (emitter assets are read-only in this version)"));
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
            ApplyNiagaraVerb(System, Op, Index, Context);
        }
    }
    bool bReady = true;
    if (!bDryRun && bCompile)
    {
        NiagaraModuleStack::MarkSystemEdited(System);
        System->RequestCompile(true);
        System->WaitForCompilationComplete(false, false);
        bReady = System->IsReadyToRun();
    }
    bool bSaved = false;
    FString SaveError;
    const bool bSaveRequired = !bDryRun && bSave
        && (Context.bChanged || System->GetOutermost()->IsDirty());
    if (bSaveRequired)
    {
        bSaved = SavePackageDirect(System, SaveError);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
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
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetBoolField(TEXT("ran"), !bDryRun && bCompile);
    Compile->SetBoolField(TEXT("ok"), bReady);
    Compile->SetNumberField(TEXT("error_count"), bReady ? 0 : 1);
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
        TSharedPtr<FJsonObject> Raw = Collaboration::StampRaw(BuildNiagaraSystemRaw(System));
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
