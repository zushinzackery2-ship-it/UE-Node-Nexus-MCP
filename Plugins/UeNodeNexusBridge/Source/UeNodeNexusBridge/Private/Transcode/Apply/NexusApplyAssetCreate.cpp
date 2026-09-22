#include "NexusApplyAssetCreate.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "NexusBlueprintAssetType.h"
#include "UeNodeNexusBridgeAssetCreateHelpers.h"
#include "UeNodeNexusBridgeAssetPaths.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
// Creation-time facts travel in the plan as set_asset_prop verbs, because the
// mirror has nowhere else to put a property that cannot be written afterwards.
FString PlanProp(const TArray<TSharedPtr<FJsonValue>>& Plan, const TCHAR* Field)
{
    for (const TSharedPtr<FJsonValue>& Value : Plan)
    {
        const TSharedPtr<FJsonObject>* Verb = nullptr;
        FString Op;
        FString Name;
        FString Text;
        if (Value.IsValid() && Value->TryGetObject(Verb) && (*Verb)->TryGetStringField(TEXT("op"), Op) && Op == TEXT("set_asset_prop")
            && (*Verb)->TryGetStringField(TEXT("name"), Name) && Name == Field && (*Verb)->TryGetStringField(TEXT("value"), Text))
        {
            return Text;
        }
    }
    return FString();
}

// A Blueprint's class is its parent class and comes from the plan; every other
// kind takes the class straight from the request.
FString CreationClass(const TArray<TSharedPtr<FJsonValue>>& Plan, const FString& Kind, const FString& AssetClass)
{
    if (Kind != TEXT("blueprint"))
    {
        return AssetClass;
    }
    const FString Parent = PlanProp(Plan, TEXT("ParentClass"));
    return Parent.IsEmpty() ? AssetClass : Parent;
}
}

bool CheckAssetForKind(
    const FString& AssetPath,
    const FString& Kind,
    const TArray<TSharedPtr<FJsonValue>>& Plan,
    const FString& AssetClass,
    FString& OutPackageName,
    FString& OutAssetName,
    FString& OutError)
{
    FText Reason;
    if (!ParseAssetPath(AssetPath, OutPackageName, OutAssetName, Reason))
    {
        OutError = Reason.ToString();
        return false;
    }
    if (FPackageName::DoesPackageExist(OutPackageName))
    {
        OutError = FString::Printf(TEXT("package already exists on disk: %s"), *OutPackageName);
        return false;
    }
    // The parent material of an instance is not a creation argument here: it
    // arrives as its own verb and is settled by EnsureMaterialParentReady.
    return CheckAssetCreate(Kind, FString(), CreationClass(Plan, Kind, AssetClass), PlanProp(Plan, TEXT("BlueprintType")), OutError);
}

UObject* CreateAssetForKind(
    const FString& AssetPath,
    const FString& Kind,
    const TArray<TSharedPtr<FJsonValue>>& Plan,
    const FString& AssetClass,
    FString& OutError)
{
    FString PackageName;
    FString AssetName;
    if (!CheckAssetForKind(AssetPath, Kind, Plan, AssetClass, PackageName, AssetName, OutError))
    {
        return nullptr;
    }
    const FString Class = CreationClass(Plan, Kind, AssetClass);
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
        Asset = CreateTypedBlueprintAsset(Package, FName(*AssetName), Class, PlanProp(Plan, TEXT("BlueprintType")), OutError);
    }
    else
    {
        Asset = CreateDataAsset(Package, FName(*AssetName), Class);
    }
    if (Asset == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("could not create %s asset %s"), *Kind, *AssetPath);
        }
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    return Asset;
}
}
