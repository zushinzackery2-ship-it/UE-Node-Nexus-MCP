#include "UeNodeNexusBridgeNiagaraHelpers.h"

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

TSharedPtr<FJsonObject> MakeNiagaraLoadFailureDetails(const FString& ObjectPath)
{
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
    Details->SetBoolField(TEXT("project_package_file_exists"), !ProjectFilename.IsEmpty() && IFileManager::Get().FileExists(*ProjectFilename));
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
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
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
        TSharedPtr<FJsonObject> Error = MakeError(TEXT("asset_not_found"), TEXT("Niagara system could not be loaded"));
        Error->SetObjectField(TEXT("details"), MakeNiagaraLoadFailureDetails(ObjectPath));
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), Error);
        return nullptr;
    }
    return System;
}
}
