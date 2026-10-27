#include "UeNodeNexusBridgeLoadedAssetStatus.h"

#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "UObject/Class.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> LoadedAssetToJson(UObject* Asset, const FString& NormalizedAssetPath)
{
    const FString PackageName = Asset != nullptr && Asset->GetPackage()
        ? Asset->GetPackage()->GetName()
        : FPackageName::ObjectPathToPackageName(NormalizedAssetPath);
    const FString AssetClassPath = Asset != nullptr && Asset->GetClass() != nullptr
        ? Asset->GetClass()->GetPathName()
        : FString();

    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("object_path"), Asset != nullptr ? Asset->GetPathName() : NormalizedAssetPath);
    Json->SetStringField(TEXT("package_name"), PackageName);
    Json->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(PackageName));
    Json->SetStringField(TEXT("asset_name"), Asset != nullptr ? Asset->GetName() : FPackageName::GetLongPackageAssetName(PackageName));
    Json->SetStringField(TEXT("asset_class_path"), AssetClassPath);
    Json->SetBoolField(TEXT("is_loaded"), Asset != nullptr);
    Json->SetBoolField(TEXT("is_redirector"), false);
    Json->SetBoolField(TEXT("asset_registry_visible"), false);
    Json->SetBoolField(TEXT("package_dirty"), Asset != nullptr && Asset->GetPackage() ? Asset->GetPackage()->IsDirty() : false);
    return Json;
}
}
