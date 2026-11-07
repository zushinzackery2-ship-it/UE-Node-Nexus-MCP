#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Move/NexusMovePreflight.h"
#include "UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MoveBatch(const FString& Operation, const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload, const TArray<TSharedPtr<FJsonValue>>& Items)
{
    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bFixRedirectors = ReadAssetBoolField(Payload, TEXT("fix_redirectors"), true);
    if (ReadAssetBoolField(Payload, TEXT("continue_on_error"), false))
    {
        return MakeInvalidAssetRequest(Operation, RequestId,
            TEXT("dependency-aware moves preflight the complete batch; continue_on_error must be false"));
    }
    TArray<AssetMove::FEntry> Entries;
    TArray<UPackage*> Packages;
    TMap<FSoftObjectPath, FSoftObjectPath> Redirects;
    TSharedPtr<FJsonObject> Data;
    FString Error;
    if (!AssetMove::Prepare(Items, Entries, Packages, Redirects, Data, Error))
    {
        auto Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(Error, TEXT("Move preflight rejected the complete batch"), Data));
        Response->SetObjectField(TEXT("data"), Data);
        Data->SetNumberField(TEXT("applied"), 0);
        return Response;
    }
    TArray<FAssetRenameData> Renames;
    TArray<UObject*> Assets;
    TArray<FString> Sources;
    for (const auto& Entry : Entries)
    {
        Renames.Emplace(Entry.Asset, Entry.Folder, Entry.Name);
        Assets.Add(Entry.Asset);
        Sources.Add(FPackageName::ObjectPathToPackageName(Entry.Source));
    }
    TGCObjectsScopeGuard<UObject> KeepAssets(Assets);
    // Keeping an empty old package alive makes native rename's reference
    // check retain its disk file. Assets protect their changing outer instead.
    Packages.RemoveAll([&Sources](UPackage* Package) { return Sources.Contains(Package->GetName()); });
    TGCObjectsScopeGuard<UPackage> KeepReferencers(Packages);
    bool bRenamed = true;
    bool bSaved = false;
    FRedirectorFixupProgress Fixup;
    TArray<TSharedPtr<FJsonValue>> SavedPackages;
    if (!bDryRun)
    {
        auto& AssetTools = FAssetToolsModule::GetModule().Get();
        bRenamed = AssetTools.RenameAssets(Renames);
        // UE rename saves packages itself. Report that side effect, and finish
        // the whole hard/soft dependency set before retiring any old path.
        if (bRenamed)
        {
            TArray<UPackage*> SaveSet = Packages;
            for (const auto& Entry : Entries)
            {
                SaveSet.AddUnique(Entry.Asset->GetOutermost());
            }
            TGCObjectsScopeGuard<UPackage> KeepTargets(SaveSet);
            AssetTools.RenameReferencingSoftObjectPaths(SaveSet, Redirects);
            bSaved = true;
            for (UPackage* Package : SaveSet)
            {
                FString SaveError;
                if (!Transcode::SavePackageDirect(Package, nullptr, SaveError))
                {
                    bSaved = false;
                    Error = FString::Printf(TEXT("save_failed: %s: %s"), *Package->GetName(), *SaveError);
                    break;
                }
                SavedPackages.Add(MakeShared<FJsonValueString>(Package->GetName()));
            }
        }
        if (bRenamed && bSaved && bFixRedirectors)
        {
            TArray<FString> Paths;
            for (const auto& Entry : Entries)
            {
                Paths.AddUnique(Entry.Folder);
                Paths.AddUnique(FPackageName::GetLongPackagePath(FPackageName::ObjectPathToPackageName(Entry.Source)));
            }
            FAssetRegistryModule::GetRegistry().ScanPathsSynchronous(Paths, true);
            Fixup = FixRedirectorPackages(Sources, false);
            Error = Fixup.Error;
        }
    }
    TArray<TSharedPtr<FJsonValue>> Results;
    TArray<TSharedPtr<FJsonValue>> Changes;
    int32 Succeeded = 0;
    int32 Moved = 0;
    for (int32 Index = 0; Index < Entries.Num(); ++Index)
    {
        const auto& Entry = Entries[Index];
        const bool bAtDestination = !bDryRun && Entry.Asset->GetPathName() == Entry.Destination;
        const bool bOk = bDryRun || (bAtDestination && bSaved && Error.IsEmpty());
        auto Item = MakeShared<FJsonObject>();
        Item->SetNumberField(TEXT("index"), Index);
        Item->SetBoolField(TEXT("ok"), bOk);
        Item->SetBoolField(TEXT("moved"), bAtDestination);
        Item->SetBoolField(TEXT("saved"), bAtDestination && bSaved);
        Item->SetStringField(TEXT("source_asset_path"), Entry.Source);
        Item->SetStringField(TEXT("destination_asset_path"), Entry.Destination);
        Item->SetStringField(TEXT("actual_asset_path"), Entry.Asset->GetPathName());
        Results.Add(MakeShared<FJsonValueObject>(Item));
        if (bAtDestination)
        {
            Changes.Add(MakeShared<FJsonValueObject>(Item));
        }
        Succeeded += bOk ? 1 : 0;
        Moved += bAtDestination ? 1 : 0;
    }
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("save"), ReadAssetBoolField(Payload, TEXT("save"), false));
    Data->SetBoolField(TEXT("native_rename_saves_packages"), true);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetBoolField(TEXT("changed"), Moved > 0);
    auto Diff = MakeShared<FJsonObject>();
    Diff->SetArrayField(TEXT("assets_moved"), Changes);
    Data->SetObjectField(TEXT("diff"), Diff);
    Data->SetBoolField(TEXT("fix_redirectors"), bFixRedirectors);
    Data->SetNumberField(TEXT("requested"), Items.Num());
    Data->SetNumberField(TEXT("attempted"), Entries.Num());
    Data->SetNumberField(TEXT("applied"), Moved);
    Data->SetNumberField(TEXT("succeeded"), Succeeded);
    Data->SetNumberField(TEXT("failed"), Entries.Num() - Succeeded);
    Data->SetArrayField(TEXT("items"), Results);
    Data->SetArrayField(TEXT("saved_packages"), SavedPackages);
    Data->SetArrayField(TEXT("redirectors_fixed"), Fixup.DeletedRedirectors);
    Data->SetArrayField(TEXT("remaining_referencers"), Fixup.RemainingReferencers);
    const bool bOk = bRenamed && Succeeded == Entries.Num();
    auto Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bOk)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_move_incomplete"),
            Error.IsEmpty() ? TEXT("Native rename did not move the complete batch; inspect actual_asset_path before retrying") : Error));
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus phase=asset_move_batch requested=%d moved=%d saved=%d ok=%d error=%s"),
        Entries.Num(), Moved, bSaved, bOk, *Error);
    return Response;
}

static TSharedPtr<FJsonObject> SingleMove(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    auto Response = MoveBatch(Operation, RequestId, Payload, { MakeShared<FJsonValueObject>(Payload) });
    if (Response->GetBoolField(TEXT("ok")))
    {
        const auto Data = Response->GetObjectField(TEXT("data"));
        const auto Item = Data->GetArrayField(TEXT("items"))[0]->AsObject();
        Data->SetObjectField(TEXT("diff"), MakeAssetOpDiff(TEXT("assets_moved"), Item));
        Data->SetBoolField(TEXT("changed"), !Data->GetBoolField(TEXT("dry_run")));
    }
    return Response;
}

TSharedPtr<FJsonObject> HandleAssetMove(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return SingleMove(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetRename(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return SingleMove(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetMoveBatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (!Payload->TryGetArrayField(TEXT("items"), Items) || !Items || Items->IsEmpty())
    {
        return MakeInvalidAssetRequest(Operation, RequestId, TEXT("items is required"));
    }
    return MoveBatch(Operation, RequestId, Payload, *Items);
}

TSharedPtr<FJsonObject> HandleAssetRenameBatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetMoveBatch(Operation, RequestId, Payload);
}
}
