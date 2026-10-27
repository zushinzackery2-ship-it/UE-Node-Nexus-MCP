#pragma once

#include "CoreMinimal.h"
#include "UeNodeNexusBridgeAssetPaths.h"

class UObject;
class UPackage;
class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeCreateData(const FString& AssetPath, const FString& AssetKind, UObject* Asset, bool bDryRun, bool bSaved);

// Every reason a create can fail before anything is written. The dry run and the
// real run ask this same question, so a preview that says "would create" is a
// promise the real call can keep.
bool CheckAssetCreate(
    const FString& AssetKind,
    const FString& ParentAssetPath,
    const FString& ParentClassPath,
    const FString& BlueprintType,
    FString& OutError);

UObject* CreateMaterialAsset(UPackage* Package, FName AssetName);
UObject* CreateMaterialInstanceAsset(UPackage* Package, FName AssetName, const FString& ParentAssetPath);
UObject* CreateMaterialFunctionAsset(UPackage* Package, FName AssetName);
UObject* CreateDataAsset(UPackage* Package, FName AssetName, const FString& ParentClassPath);
UObject* CreateTextureRenderTarget2DAsset(UPackage* Package, FName AssetName);

bool IsDiscardedAssetObject(UObject* Object);
bool ReleaseDiscardedAssetObject(const FString& ObjectPath);

TSharedPtr<FJsonObject> BuildAssetCreateConflictData(
    const FString& PackageName,
    const FString& ObjectPath,
    UObject* ExistingObject,
    bool bPackageExists,
    const FString& PackageFilename);
}
