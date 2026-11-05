#include "NexusAssetDeletion.h"
#include "NexusAssetDeleteOrder.h"
#include "NexusAssetDeleteResult.h"
#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Assets/Management/NexusPackageLoadAudit.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
using AssetDeletion::FClosureMember;

bool ReadDeleteCleanupField(const TSharedPtr<FJsonObject>& Payload)
{
    if (Payload->HasTypedField<EJson::Boolean>(TEXT("cleanup_after_delete")))
    {
        return ReadAssetBoolField(Payload, TEXT("cleanup_after_delete"), true);
    }
    return ReadAssetBoolField(Payload, TEXT("release_package_after_delete"), true);
}

void ReleasePackages(const TArray<FString>& PackageNames)
{
    TArray<UPackage*> Packages;
    for (const FString& Name : PackageNames)
    {
        if (UPackage* Package = FindPackage(nullptr, *Name))
        {
            Packages.Add(Package);
        }
    }
    if (!Packages.IsEmpty())
    {
        if (GEditor)
        {
            GEditor->ResetTransaction(NSLOCTEXT("UeNodeNexusBridge", "ReleasePackageAfterDelete", "UeNodeNexusBridge release deleted asset packages"));
        }
        UPackageTools::FUnloadPackageParams UnloadParams(Packages);
        UnloadParams.bResetTransBuffer = false;
        UPackageTools::UnloadPackages(UnloadParams);
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

TSharedPtr<FJsonObject> ReferencedResponse(const FString& Operation, const FString& RequestId,
    const TArray<FName>& Referencers, const TArray<TSharedPtr<FJsonValue>>& Rows)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_has_referencers"),
        TEXT("Assets outside the requested set reference it; include them or set allow_referenced to true")));
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedPtr<FJsonValue>& Row : Rows)
    {
        const TSharedPtr<FJsonObject> Item = Row->AsObject();
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("warning"), TEXT("asset_has_referencers"),
            TEXT("Referenced by ") + Item->GetStringField(TEXT("referencer")), Item->GetStringField(TEXT("asset_path")), TEXT("UeNodeNexusBridge"))));
    }
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("referencers"), PackageNamesToJson(Referencers));
    Data->SetArrayField(TEXT("external_referencers"), Rows);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

}

// Deletes an audited asset set as one transaction. Every member is loaded while
// the whole set still exists on disk, then the set is deleted in one call:
// deleting member by member made later loads resolve imports into packages that
// were already half deleted.
TSharedPtr<FJsonObject> DeleteAssetSet(const FString& Operation, const FString& RequestId,
    const TArray<FString>& AssetPaths, const TSharedPtr<FJsonObject>& Payload)
{
    const bool bDryRun = ReadAssetBoolField(Payload, TEXT("dry_run"), true);
    const bool bAllowReferenced = ReadAssetBoolField(Payload, TEXT("allow_referenced"), false);
    const bool bCleanupAfterDelete = ReadDeleteCleanupField(Payload);
    TArray<FClosureMember> Members;
    TSet<FName> Owned;
    for (const FString& AssetPath : AssetPaths)
    {
        FString PackageName;
        FString PackagePath;
        FString AssetName;
        FText Reason;
        if (!SplitAssetObjectPath(AssetPath, PackageName, PackagePath, AssetName, Reason))
        {
            return MakeInvalidAssetRequest(Operation, RequestId, FString::Printf(TEXT("invalid asset path %s: %s"), *AssetPath, *Reason.ToString()));
        }
        if (Owned.Contains(FName(*PackageName)))
        {
            return MakeInvalidAssetRequest(Operation, RequestId, TEXT("asset_paths names one package twice: ") + PackageName);
        }
        // An asset that was created but never saved has no file and is still deletable.
        if (!FPackageName::DoesPackageExist(PackageName) && FindPackage(nullptr, *PackageName) == nullptr)
        {
            return MakeAssetNotFoundResponse(Operation, RequestId, AssetPath);
        }
        Owned.Add(FName(*PackageName));
        Members.Add({ AssetPath, FName(*PackageName) });
    }

    TArray<FName> External;
    TArray<TSharedPtr<FJsonValue>> ExternalRows;
    for (const FClosureMember& Member : Members)
    {
        TArray<FName> Referencers;
        FAssetRegistryModule::GetRegistry().GetReferencers(Member.PackageName, Referencers);
        for (const FName Referencer : Referencers)
        {
            if (Owned.Contains(Referencer))
            {
                continue;
            }
            External.AddUnique(Referencer);
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("asset_path"), Member.AssetPath);
            Row->SetStringField(TEXT("referencer"), Referencer.ToString());
            ExternalRows.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    if (!External.IsEmpty() && !bAllowReferenced)
    {
        return ReferencedResponse(Operation, RequestId, External, ExternalRows);
    }

    TArray<FString> OrderedPaths;
    TArray<FString> PackageNames;
    for (const int32 Member : AssetDeletion::DependentsFirst(Members))
    {
        OrderedPaths.Add(Members[Member].AssetPath);
        PackageNames.Add(Members[Member].PackageName.ToString());
    }
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("asset_path"), AssetPaths[0]);
    Item->SetArrayField(TEXT("order"), AssetDeletion::PathValues(OrderedPaths));
    Item->SetArrayField(TEXT("packages"), AssetDeletion::PathValues(PackageNames));
    Item->SetArrayField(TEXT("referencers"), PackageNamesToJson(External));
    Item->SetArrayField(TEXT("external_referencers"), ExternalRows);
    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        TSharedPtr<FJsonObject> Data = MakeAssetWriteData(true, false, true, MakeAssetOpDiff(TEXT("assets_deleted"), Item), PackageNames[0], false, false);
        Data->SetNumberField(TEXT("requested"), Members.Num());
        Data->SetArrayField(TEXT("deleted_assets"), {});
        Data->SetArrayField(TEXT("remaining_assets"), AssetDeletion::PathValues(OrderedPaths));
        Data->SetBoolField(TEXT("cleanup_after_delete"), bCleanupAfterDelete);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    TArray<TSharedPtr<FJsonValue>> Loads;
    TArray<TSharedPtr<FJsonValue>> Incomplete;
    const TArray<FPackageLoadAudit> Audits = LoadPackagesAudited(PackageNames);
    for (const FPackageLoadAudit& Audit : Audits)
    {
        const auto Row = MakeShared<FJsonValueObject>(Audit.ToJson());
        Loads.Add(Row);
        if (!Audit.IsComplete() || Audit.Package->HasAnyPackageFlags(PKG_CompiledIn))
        {
            Incomplete.Add(Row);
        }
    }
    Item->SetArrayField(TEXT("loads"), Loads);
    Item->SetArrayField(TEXT("incomplete_packages"), Incomplete);
    if (!Incomplete.IsEmpty())
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=asset_delete_preflight ok=0 incomplete_packages=%d"), *RequestId, Incomplete.Num());
        auto Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_load_incomplete"),
            TEXT("Deletion refused because a package did not load completely: ") + Incomplete[0]->AsObject()->GetStringField(TEXT("package"))));
        Response->SetObjectField(TEXT("data"), MakeAssetWriteData(false, false, false,
            MakeAssetOpDiff(TEXT("assets_deleted"), Item), PackageNames[0], false, false));
        return Response;
    }
    TArray<UObject*> Objects;
    TArray<TWeakObjectPtr<UObject>> WeakObjects;
    TArray<AssetDeletion::FDeleteTarget> Targets;
    for (int32 Index = 0; Index < OrderedPaths.Num(); ++Index)
    {
        UObject* Asset = LoadAssetForManagement(OrderedPaths[Index]);
        if (Asset == nullptr)
        {
            TSharedPtr<FJsonObject> Response = MakeAssetNotFoundResponse(Operation, RequestId, OrderedPaths[Index]);
            TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
            Data->SetBoolField(TEXT("deleted"), false);
            Data->SetArrayField(TEXT("loads"), Loads);
            Response->SetObjectField(TEXT("data"), Data);
            return Response;
        }
        Objects.Add(Asset);
        WeakObjects.Add(Asset);
        Targets.Add({ Asset->GetPathName(), Asset->GetOutermost()->GetName(), false });
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=asset_delete_begin members=%d external_referencers=%d"), *RequestId, Objects.Num(), External.Num());
    const int32 Deleted = ObjectTools::DeleteObjectsUnchecked(Objects);
    for (int32 Index = 0; Index < Targets.Num(); ++Index)
    {
        UObject* Remaining = WeakObjects[Index].Get();
        Targets[Index].bRemovedFromMemory = Remaining == nullptr || Remaining->GetPathName() != Targets[Index].AssetPath;
    }
    if (bCleanupAfterDelete)
    {
        ReleasePackages(PackageNames);
    }
    return AssetDeletion::Result(Operation, RequestId, Targets, External, Deleted, bCleanupAfterDelete, Item);
}
}
