#include "UeNodeNexusBridgeAssetCreateHelpers.h"

#include "NexusBlueprintAssetType.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/DataAsset.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/TextureRenderTargetFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeDataAsset.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
bool ParseAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FText& OutReason)
{
    const int32 LastSlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 LastDotIndex = AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const bool bLooksLikeObjectPath = LastDotIndex > LastSlashIndex;

    if (bLooksLikeObjectPath && FPackageName::IsValidObjectPath(AssetPath, &OutReason))
    {
        OutPackageName = FPackageName::ObjectPathToPackageName(AssetPath);
        OutAssetName = FPackageName::ObjectPathToObjectName(AssetPath);
    }
    else if (FPackageName::IsValidLongPackageName(AssetPath, false, &OutReason))
    {
        OutPackageName = AssetPath;
        OutAssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    }
    else
    {
        return false;
    }

    return FPackageName::IsValidLongPackageName(OutPackageName, false, &OutReason) && !OutAssetName.IsEmpty();
}

static TSharedPtr<FJsonObject> MakeCreateDiff(const FString& AssetPath, const FString& AssetKind, const FString& AssetClass)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("asset_path"), AssetPath);
    Item->SetStringField(TEXT("asset_kind"), AssetKind);
    Item->SetStringField(TEXT("asset_class"), AssetClass);

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    Diff->SetArrayField(TEXT("assets_created"), { MakeShared<FJsonValueObject>(Item) });
    return Diff;
}

TSharedPtr<FJsonObject> MakeCreateData(const FString& AssetPath, const FString& AssetKind, UObject* Asset, bool bDryRun, bool bSaved)
{
    const FString AssetClass = Asset ? Asset->GetClass()->GetPathName() : FString();
    TSharedPtr<FJsonObject> Data = MakeWriteData(
        bDryRun,
        !bDryRun && Asset != nullptr,
        !bDryRun && Asset != nullptr,
        MakeCreateDiff(AssetPath, AssetKind, AssetClass),
        MakePinIntegrity(true, {}, {}),
        MakeCompilePostCheck(false, false, true, 0, 0),
        MakeDirtyState(Asset));

    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_kind"), AssetKind);
    Data->SetStringField(TEXT("asset_class"), AssetClass);

    const TSharedPtr<FJsonObject>* PostChecks = nullptr;
    if (Data->TryGetObjectField(TEXT("post_checks"), PostChecks) && PostChecks != nullptr)
    {
        const TSharedPtr<FJsonObject>* DirtyState = nullptr;
        if ((*PostChecks)->TryGetObjectField(TEXT("dirty_state"), DirtyState) && DirtyState != nullptr)
        {
            (*DirtyState)->SetBoolField(TEXT("saved"), bSaved);
        }
    }
    return Data;
}

UObject* CreateMaterialAsset(UPackage* Package, FName AssetName)
{
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    return Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

UObject* CreateMaterialInstanceAsset(UPackage* Package, FName AssetName, const FString& ParentAssetPath)
{
    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    if (!ParentAssetPath.IsEmpty())
    {
        Factory->InitialParent = LoadObject<UMaterialInterface>(nullptr, *ParentAssetPath);
        if (Factory->InitialParent == nullptr)
        {
            return nullptr;
        }
    }
    return Factory->FactoryCreateNew(UMaterialInstanceConstant::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

UObject* CreateMaterialFunctionAsset(UPackage* Package, FName AssetName)
{
    UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
    return Factory->FactoryCreateNew(UMaterialFunction::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

UObject* CreateDataAsset(UPackage* Package, FName AssetName, const FString& ParentClassPath)
{
    UClass* DataAssetClass = UUeNodeNexusBridgeDataAsset::StaticClass();
    if (!ParentClassPath.IsEmpty())
    {
        DataAssetClass = LoadObject<UClass>(nullptr, *ParentClassPath);
    }
    if (DataAssetClass == nullptr || !DataAssetClass->IsChildOf(UDataAsset::StaticClass()))
    {
        return nullptr;
    }
    UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
    Factory->DataAssetClass = DataAssetClass;
    return Factory->FactoryCreateNew(DataAssetClass, Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

bool CheckAssetCreate(
    const FString& AssetKind,
    const FString& ParentAssetPath,
    const FString& ParentClassPath,
    const FString& BlueprintType,
    FString& OutError)
{
    if (AssetKind.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase))
    {
        return CheckBlueprintAsset(ParentClassPath, BlueprintType, OutError);
    }
    if (AssetKind.Equals(TEXT("material_instance"), ESearchCase::IgnoreCase))
    {
        if (!ParentAssetPath.IsEmpty() && LoadObject<UMaterialInterface>(nullptr, *ParentAssetPath) == nullptr)
        {
            OutError = FString::Printf(TEXT("parent material could not be loaded: %s"), *ParentAssetPath);
            return false;
        }
        return true;
    }
    if (AssetKind.Equals(TEXT("data_asset"), ESearchCase::IgnoreCase) || AssetKind.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
    {
        if (ParentClassPath.IsEmpty())
        {
            return true;
        }
        const UClass* DataAssetClass = LoadObject<UClass>(nullptr, *ParentClassPath);
        if (DataAssetClass == nullptr)
        {
            OutError = FString::Printf(TEXT("data asset class could not be loaded: %s"), *ParentClassPath);
            return false;
        }
        if (!DataAssetClass->IsChildOf(UDataAsset::StaticClass()))
        {
            OutError = FString::Printf(TEXT("class '%s' does not derive from DataAsset"), *ParentClassPath);
            return false;
        }
        return true;
    }
    static const TCHAR* Parameterless[] = { TEXT("material"), TEXT("material_function"), TEXT("texture_render_target_2d") };
    for (const TCHAR* Kind : Parameterless)
    {
        if (AssetKind.Equals(Kind, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    OutError = FString::Printf(TEXT("unsupported asset_kind: %s"), *AssetKind);
    return false;
}

UObject* CreateTextureRenderTarget2DAsset(UPackage* Package, FName AssetName)
{
    UTextureRenderTargetFactoryNew* Factory = NewObject<UTextureRenderTargetFactoryNew>();
    Factory->Width = 256;
    Factory->Height = 256;
    return Factory->FactoryCreateNew(UTextureRenderTarget2D::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

bool IsDiscardedAssetObject(UObject* Object)
{
    return Object != nullptr && !Object->HasAnyFlags(RF_Public | RF_Standalone);
}

bool ReleaseDiscardedAssetObject(const FString& ObjectPath)
{
    if (GEditor)
    {
        GEditor->ResetTransaction(NSLOCTEXT("UeNodeNexusBridge", "ReleaseDiscardedAssetObject", "UeNodeNexusBridge release discarded asset object"));
    }
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    if (UObject* ExistingObject = FindObject<UObject>(nullptr, *ObjectPath))
    {
        if (IsDiscardedAssetObject(ExistingObject))
        {
            ExistingObject->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
            ExistingObject->MarkAsGarbage();
            CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        }
    }
    return FindObject<UObject>(nullptr, *ObjectPath) == nullptr;
}

TSharedPtr<FJsonObject> BuildAssetCreateConflictData(
    const FString& PackageName,
    const FString& ObjectPath,
    UObject* ExistingObject,
    bool bPackageExists,
    const FString& PackageFilename)
{
    FAssetData RegistryData = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("package_name"), PackageName);
    Data->SetStringField(TEXT("object_path"), ObjectPath);
    Data->SetBoolField(TEXT("memory_object_exists"), ExistingObject != nullptr);
    Data->SetBoolField(TEXT("package_file_exists"), bPackageExists);
    Data->SetStringField(TEXT("package_filename"), PackageFilename);
    Data->SetBoolField(TEXT("asset_registry_exists"), RegistryData.IsValid());

    if (ExistingObject != nullptr)
    {
        Data->SetStringField(TEXT("memory_object_class"), ExistingObject->GetClass()->GetPathName());
        Data->SetBoolField(TEXT("memory_object_is_asset"), ExistingObject->IsAsset());
        Data->SetBoolField(TEXT("memory_object_public"), ExistingObject->HasAnyFlags(RF_Public));
        Data->SetBoolField(TEXT("memory_object_standalone"), ExistingObject->HasAnyFlags(RF_Standalone));
        Data->SetBoolField(TEXT("memory_object_discarded_asset"), IsDiscardedAssetObject(ExistingObject));
    }
    if (RegistryData.IsValid())
    {
        Data->SetStringField(TEXT("registry_class"), RegistryData.AssetClassPath.ToString());
        Data->SetBoolField(TEXT("registry_loaded"), RegistryData.IsAssetLoaded());
        Data->SetBoolField(TEXT("registry_redirector"), RegistryData.IsRedirector());
    }
    return Data;
}
}
