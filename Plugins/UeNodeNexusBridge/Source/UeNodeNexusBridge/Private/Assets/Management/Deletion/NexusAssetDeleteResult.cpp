#include "NexusAssetDeleteResult.h"

#include "Assets/Management/UeNodeNexusBridgeAssetManagementShared.h"
#include "Engine/Blueprint.h"
#include "Misc/PackagePath.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UObject/Package.h"
#include "UObject/PackageResourceManager.h"

namespace UeNodeNexusBridge::AssetDeletion
{
TArray<TSharedPtr<FJsonValue>> PathValues(const TArray<FString>& Paths)
{
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Path : Paths)
    {
        Values.Add(MakeShared<FJsonValueString>(Path));
    }
    return Values;
}

TSharedPtr<FJsonObject> Result(const FString& Operation, const FString& RequestId,
    const TArray<FDeleteTarget>& Targets, const TArray<FName>& External, int32 DeletedObjects,
    bool bCleanupAfterDelete, const TSharedPtr<FJsonObject>& Item)
{
    TArray<FString> OnDisk;
    TArray<FString> Resident;
    TArray<FString> DeletedAssets;
    TArray<FString> ModifiedAssets;
    TArray<FString> RemainingAssets;
    for (const FDeleteTarget& Target : Targets)
    {
        // FPackageName can answer from the asset registry before its delete event settles.
        const bool bOnDisk = IPackageResourceManager::Get().DoesPackageExist(FPackagePath::FromPackageNameChecked(Target.PackageName));
        if (bOnDisk)
        {
            OnDisk.Add(Target.PackageName);
        }
        if (FindPackage(nullptr, *Target.PackageName) != nullptr)
        {
            Resident.Add(Target.PackageName);
        }
        if (Target.bRemovedFromMemory && !bOnDisk)
        {
            DeletedAssets.Add(Target.AssetPath);
            continue;
        }
        RemainingAssets.Add(Target.AssetPath);
        if (Target.bRemovedFromMemory)
        {
            ModifiedAssets.Add(Target.AssetPath);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Affected;
    for (const FName Referencer : External)
    {
        UPackage* Package = FindPackage(nullptr, *Referencer.ToString());
        if (Package == nullptr)
        {
            continue;
        }
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Package->GetName());
        Row->SetBoolField(TEXT("blueprint"), FindObjectWithOuter(Package, UBlueprint::StaticClass()) != nullptr);
        Affected.Add(MakeShared<FJsonValueObject>(Row));
    }
    const bool bDeleted = RemainingAssets.IsEmpty() && DeletedObjects >= Targets.Num();
    const bool bChanged = DeletedObjects > 0 || !DeletedAssets.IsEmpty() || !ModifiedAssets.IsEmpty();
    const bool bReleased = bCleanupAfterDelete && Resident.IsEmpty();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=asset_delete_complete ok=%d changed=%d deleted_objects=%d deleted_assets=%d remaining_assets=%d packages_on_disk=%d residual_packages=%d"),
        *RequestId, bDeleted, bChanged, DeletedObjects, DeletedAssets.Num(), RemainingAssets.Num(), OnDisk.Num(), Resident.Num());
    Item->SetArrayField(TEXT("packages_on_disk"), PathValues(OnDisk));
    Item->SetArrayField(TEXT("residual_packages"), PathValues(Resident));
    Item->SetArrayField(TEXT("loaded_external_referencers"), Affected);
    auto Response = MakeEnvelope(Operation, RequestId, bDeleted);
    auto Data = MakeAssetWriteData(false, bDeleted, bChanged, MakeAssetOpDiff(TEXT("assets_deleted"), Item),
        Targets[0].PackageName, false, bDeleted);
    Data->SetNumberField(TEXT("requested"), Targets.Num());
    Data->SetNumberField(TEXT("deleted_objects"), DeletedObjects);
    Data->SetArrayField(TEXT("deleted_assets"), PathValues(DeletedAssets));
    Data->SetArrayField(TEXT("modified_assets"), PathValues(ModifiedAssets));
    Data->SetArrayField(TEXT("remaining_assets"), PathValues(RemainingAssets));
    Data->SetBoolField(TEXT("cleanup_after_delete"), bCleanupAfterDelete);
    Data->SetBoolField(TEXT("memory_released"), bReleased);
    Data->SetBoolField(TEXT("released_package"), bReleased);
    Data->SetBoolField(TEXT("package_still_loaded"), !Resident.IsEmpty());
    Response->SetObjectField(TEXT("data"), Data);
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    if (!Resident.IsEmpty() && bCleanupAfterDelete)
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("warning"), TEXT("deleted_packages_still_loaded"),
            FString::Printf(TEXT("%d deleted packages are still resident after cleanup"), Resident.Num()), Resident[0], TEXT("UeNodeNexusBridge"))));
    }
    if (!Affected.IsEmpty())
    {
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("warning"), TEXT("loaded_referencers_of_deleted_assets"),
            FString::Printf(TEXT("%d loaded packages outside the set referenced deleted assets"), Affected.Num()),
            Affected[0]->AsObject()->GetStringField(TEXT("package")), TEXT("UeNodeNexusBridge"))));
    }
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    if (!bDeleted)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_delete_failed"),
            FString::Printf(TEXT("deleted %d of %d assets; packages left on disk: %s"), DeletedAssets.Num(), Targets.Num(), *FString::Join(OnDisk, TEXT(", ")))));
    }
    return Response;
}
}
