#include "UeNodeNexusBridgeTranscode.h"
#include "Apply/UeNodeNexusBridgeTranscodeApplyResult.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeAssetCreateHelpers.h"
#include "UeNodeNexusBridgeAssetPaths.h"
#include "UeNodeNexusBridgeDiagnostics.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;

namespace Transcode
{
// Blueprints and DataAssets take their class at creation; the plan carries it as a set_asset_prop.
static FString PlanCreationClass(const TArray<TSharedPtr<FJsonValue>>& Plan, const FString& Kind, const FString& AssetClass)
{
    if (Kind != TEXT("blueprint"))
    {
        return AssetClass;
    }
    for (const TSharedPtr<FJsonValue>& Value : Plan)
    {
        const TSharedPtr<FJsonObject>* Verb = nullptr;
        FString Op;
        FString Name;
        FString Text;
        if (Value.IsValid() && Value->TryGetObject(Verb) && (*Verb)->TryGetStringField(TEXT("op"), Op) && Op == TEXT("set_asset_prop")
            && (*Verb)->TryGetStringField(TEXT("name"), Name) && Name == TEXT("ParentClass") && (*Verb)->TryGetStringField(TEXT("value"), Text))
        {
            return Text;
        }
    }
    return FString();
}

UObject* CreateAssetForKind(const FString& AssetPath, const FString& Kind, const FString& AssetClass, FString& OutError)
{
    FString PackageName;
    FString AssetName;
    FText Reason;
    if (!ParseAssetPath(AssetPath, PackageName, AssetName, Reason))
    {
        OutError = Reason.ToString();
        return nullptr;
    }
    if (FPackageName::DoesPackageExist(PackageName))
    {
        OutError = FString::Printf(TEXT("package already exists on disk: %s"), *PackageName);
        return nullptr;
    }
    UPackage* Package = CreatePackage(*PackageName);
    UObject* Asset = nullptr;
    if (Kind == TEXT("material"))
    {
        Asset = CreateMaterialAsset(Package, FName(*AssetName));
    }
    else if (Kind == TEXT("material_function"))
    {
        Asset = CreateMaterialFunctionAsset(Package, FName(*AssetName));
    }
    else if (Kind == TEXT("material_instance"))
    {
        Asset = CreateMaterialInstanceAsset(Package, FName(*AssetName), FString());
    }
    else if (Kind == TEXT("blueprint"))
    {
        Asset = CreateBlueprintAsset(Package, FName(*AssetName), AssetClass);
    }
    else if (Kind == TEXT("asset"))
    {
        Asset = CreateDataAsset(Package, FName(*AssetName), AssetClass);
    }
    if (Asset == nullptr)
    {
        OutError = FString::Printf(TEXT("could not create %s asset %s"), *Kind, *AssetPath);
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    return Asset;
}
}

static TSharedPtr<FJsonObject> MakeCompileJson(const FBridgeAssetCompileDiagnostics& Compile, bool bRequested)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetBoolField(TEXT("requested"), bRequested);
    Json->SetBoolField(TEXT("ran"), Compile.bRan);
    Json->SetBoolField(TEXT("ok"), !Compile.bRan || Compile.bOk);
    Json->SetNumberField(TEXT("error_count"), Compile.ErrorCount);
    Json->SetNumberField(TEXT("warning_count"), Compile.WarningCount);
    return Json;
}

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

TSharedPtr<FJsonObject> HandleTranscodeApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
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
    bool bCreated = false;
    if (Asset == nullptr)
    {
        if (!bCreate)
        {
            return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("asset could not be loaded: %s"), *AssetPath));
        }
        if (bDryRun)
        {
            TSharedPtr<FJsonObject> DryData = MakeShared<FJsonObject>();
            DryData->SetBoolField(TEXT("dry_run"), true);
            DryData->SetBoolField(TEXT("would_create"), true);
            DryData->SetNumberField(TEXT("applied"), 0);
            TSharedPtr<FJsonObject> DryResponse = MakeEnvelope(Operation, RequestId, true);
            DryResponse->SetObjectField(TEXT("data"), DryData);
            return DryResponse;
        }
        FString Error;
        Asset = CreateAssetForKind(AssetPath, Kind, PlanCreationClass(*Plan, Kind, AssetClass), Error);
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

    FBridgeAssetCompileDiagnostics Compile;
    if (!bDryRun && bCompile)
    {
        if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
        {
            UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
            Compile.bSupported = true;
            Compile.bRan = true;
            Compile.bOk = true;
        }
        else
        {
            Compile = CollectAssetCompileDiagnostics(Asset, AssetPath, true);
        }
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
    if (bSaveRequired)
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
    Data->SetArrayField(TEXT("diagnostics"), Compile.Diagnostics);
    Data->SetObjectField(TEXT("compile"), MakeCompileJson(Compile, bCompile && !bDryRun));
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
}
