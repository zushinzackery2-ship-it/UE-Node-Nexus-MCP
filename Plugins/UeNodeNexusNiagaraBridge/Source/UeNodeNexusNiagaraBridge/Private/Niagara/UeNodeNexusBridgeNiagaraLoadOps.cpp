#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
FString NormalizeNiagaraAssetPath(const FString& AssetPath)
{
    FText Reason;
    const int32 LastSlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 LastDotIndex = AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (LastDotIndex > LastSlashIndex && FPackageName::IsValidObjectPath(AssetPath, &Reason))
    {
        return AssetPath;
    }
    if (FPackageName::IsValidLongPackageName(AssetPath, false, &Reason))
    {
        return AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    }
    return AssetPath;
}

void ScanNiagaraAssetFolder(const FString& ObjectPath)
{
    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
    const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
    if (PackagePath.StartsWith(TEXT("/")) && !PackagePath.Contains(TEXT(".")))
    {
        FAssetRegistryModule::GetRegistry().ScanPathsSynchronous({ PackagePath }, true);
    }
}

UNiagaraSystem* LoadNiagaraSystemPackageFallback(const FString& ObjectPath)
{
    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
    UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
    if (Package == nullptr && PackageName.StartsWith(TEXT("/Game/")))
    {
        const FString RelativePath = PackageName.RightChop(6) + FPackageName::GetAssetPackageExtension();
        const FString ProjectFilename = FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
        if (IFileManager::Get().FileExists(*ProjectFilename))
        {
            Package = LoadPackage(nullptr, *ProjectFilename, LOAD_None);
        }
    }
    if (Package == nullptr)
    {
        return nullptr;
    }
    const FString ObjectName = FPackageName::ObjectPathToObjectName(ObjectPath);
    return FindObject<UNiagaraSystem>(Package, *ObjectName);
}

// Builds the load-failure diagnostics and decides, via an explicit out-param,
// whether the failure is a type mismatch: an asset DOES exist at the path but it
// is not a NiagaraSystem (e.g. a Cascade UParticleSystem, a Material, etc.). The
// verdict is returned explicitly rather than inferred from a details field so the
// error code can never silently flip when the details shape changes.
TSharedPtr<FJsonObject> MakeNiagaraLoadFailureDetails(const FString& ObjectPath, bool& bOutTypeMismatch)
{
    bOutTypeMismatch = false;

    const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
    FString PackageFilename;
    FPackageName::TryConvertLongPackageNameToFilename(PackageName, PackageFilename, FPackageName::GetAssetPackageExtension());
    const FString ProjectFilename = PackageName.StartsWith(TEXT("/Game/"))
        ? FPaths::Combine(FPaths::ProjectContentDir(), PackageName.RightChop(6) + FPackageName::GetAssetPackageExtension())
        : FString();

    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("object_path"), ObjectPath);
    Details->SetStringField(TEXT("package_name"), PackageName);
    Details->SetStringField(TEXT("package_filename"), PackageFilename);
    Details->SetBoolField(TEXT("package_file_exists"), IFileManager::Get().FileExists(*PackageFilename));
    Details->SetStringField(TEXT("project_package_filename"), ProjectFilename);

    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAssetsByPackageName(FName(*PackageName), Assets);
    if (Assets.Num() > 0)
    {
        const FAssetData& AssetData = Assets[0];
        Details->SetStringField(TEXT("asset_class_path"), AssetData.AssetClassPath.ToString());
        const FTopLevelAssetPath NiagaraSystemClass = UNiagaraSystem::StaticClass()->GetClassPathName();
        if (AssetData.AssetClassPath != NiagaraSystemClass)
        {
            bOutTypeMismatch = true;
            Details->SetStringField(TEXT("expected"), NiagaraSystemClass.ToString());
            Details->SetStringField(TEXT("actual"), AssetData.AssetClassPath.ToString());
        }
    }
    return Details;
}
}

UNiagaraSystem* LoadNiagaraSystemFromPayload(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& Operation,
    const FString& RequestId,
    TSharedPtr<FJsonObject>& OutResponse)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
        return nullptr;
    }

    const FString ObjectPath = NormalizeNiagaraAssetPath(AssetPath);
    UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
    if (System == nullptr)
    {
        ScanNiagaraAssetFolder(ObjectPath);
        System = LoadObject<UNiagaraSystem>(nullptr, *ObjectPath);
    }
    if (System == nullptr)
    {
        System = LoadNiagaraSystemPackageFallback(ObjectPath);
    }
    if (System == nullptr)
    {
        bool bTypeMismatch = false;
        TSharedPtr<FJsonObject> Details = MakeNiagaraLoadFailureDetails(ObjectPath, bTypeMismatch);
        OutResponse = MakeOperationError(
            Operation,
            RequestId,
            bTypeMismatch ? TEXT("asset_type_mismatch") : TEXT("asset_not_found"),
            bTypeMismatch ? TEXT("asset_path is not a NiagaraSystem") : TEXT("Niagara system could not be loaded"),
            Details);
        return nullptr;
    }
    return System;
}
}
