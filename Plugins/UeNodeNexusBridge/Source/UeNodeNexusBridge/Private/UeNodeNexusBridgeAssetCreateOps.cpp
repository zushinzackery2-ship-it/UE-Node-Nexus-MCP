#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "FileHelpers.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
static bool ParseAssetPath(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FText& OutReason)
{
    if (!FPackageName::IsValidObjectPath(AssetPath, &OutReason))
    {
        return false;
    }

    OutPackageName = FPackageName::ObjectPathToPackageName(AssetPath);
    OutAssetName = FPackageName::ObjectPathToObjectName(AssetPath);
    if (!FPackageName::IsValidLongPackageName(OutPackageName, false, &OutReason) || OutAssetName.IsEmpty())
    {
        return false;
    }
    return true;
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

static TSharedPtr<FJsonObject> MakeCreateData(const FString& AssetPath, const FString& AssetKind, UObject* Asset, bool bDryRun, bool bSaved)
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

static UObject* CreateMaterialAsset(UPackage* Package, const FName AssetName)
{
    UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
    return Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, AssetName, RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
}

static UObject* CreateMaterialInstanceAsset(UPackage* Package, const FName AssetName, const FString& ParentAssetPath)
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

static UObject* CreateBlueprintAsset(UPackage* Package, const FName AssetName, const FString& ParentClassPath)
{
    UClass* ParentClass = AActor::StaticClass();
    if (!ParentClassPath.IsEmpty())
    {
        ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
    }
    if (ParentClass == nullptr || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        return nullptr;
    }
    return FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, AssetName, BPTYPE_Normal);
}

TSharedPtr<FJsonObject> HandleAssetCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    FString AssetKind;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || !Payload->TryGetStringField(TEXT("asset_kind"), AssetKind))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("asset_path and asset_kind are required")));
        return Response;
    }

    FString PackageName;
    FString AssetName;
    FText Reason;
    if (!ParseAssetPath(AssetPath, PackageName, AssetName, Reason))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_asset_path"), Reason.ToString()));
        return Response;
    }

    if (FindObject<UObject>(nullptr, *AssetPath) != nullptr || FPackageName::DoesPackageExist(PackageName))
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_already_exists"), TEXT("Asset package already exists")));
        return Response;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), MakeCreateData(AssetPath, AssetKind, nullptr, true, false));
        return Response;
    }

    UPackage* Package = CreatePackage(*PackageName);
    UObject* Asset = nullptr;
    FString ParentAssetPath;
    FString ParentClassPath;
    Payload->TryGetStringField(TEXT("parent_asset_path"), ParentAssetPath);
    Payload->TryGetStringField(TEXT("parent_class_path"), ParentClassPath);

    if (AssetKind.Equals(TEXT("material"), ESearchCase::IgnoreCase))
    {
        Asset = CreateMaterialAsset(Package, FName(*AssetName));
    }
    else if (AssetKind.Equals(TEXT("material_instance"), ESearchCase::IgnoreCase))
    {
        Asset = CreateMaterialInstanceAsset(Package, FName(*AssetName), ParentAssetPath);
    }
    else if (AssetKind.Equals(TEXT("blueprint"), ESearchCase::IgnoreCase))
    {
        Asset = CreateBlueprintAsset(Package, FName(*AssetName), ParentClassPath);
    }

    if (Asset == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("asset_create_failed"), TEXT("Asset kind is unsupported or required parent could not be loaded")));
        return Response;
    }

    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    Asset->PostEditChange();

    const bool bSaved = bSave && UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, !bSave || bSaved);
    Response->SetObjectField(TEXT("data"), MakeCreateData(Asset->GetPathName(), AssetKind, Asset, false, bSaved));
    if (bSave && !bSaved)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("save_failed"), TEXT("Asset was created but package save failed")));
    }
    return Response;
}
}
