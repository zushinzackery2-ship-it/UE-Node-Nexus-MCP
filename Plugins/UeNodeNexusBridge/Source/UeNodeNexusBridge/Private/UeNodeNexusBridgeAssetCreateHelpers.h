#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;
class FJsonObject;

namespace UeNodeNexusBridge
{
bool ParseAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FText& OutReason);

TSharedPtr<FJsonObject> MakeCreateData(const FString& AssetPath, const FString& AssetKind, UObject* Asset, bool bDryRun, bool bSaved);

UObject* CreateMaterialAsset(UPackage* Package, FName AssetName);
UObject* CreateMaterialInstanceAsset(UPackage* Package, FName AssetName, const FString& ParentAssetPath);
UObject* CreateBlueprintAsset(UPackage* Package, FName AssetName, const FString& ParentClassPath);
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
