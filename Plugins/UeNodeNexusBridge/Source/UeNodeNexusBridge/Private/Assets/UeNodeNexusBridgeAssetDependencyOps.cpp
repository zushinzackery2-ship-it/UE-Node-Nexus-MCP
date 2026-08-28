#include "UeNodeNexusBridgeOperations.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString PackageNameFromAssetPath(const FString& AssetPath)
{
    FString PackageName = AssetPath;
    int32 DotIndex = INDEX_NONE;
    if (PackageName.FindChar(TEXT('.'), DotIndex))
    {
        PackageName.LeftInline(DotIndex);
    }
    return PackageName;
}

bool ShouldSkipPackage(const FName& PackageName, bool bIncludeEngine)
{
    if (bIncludeEngine)
    {
        return false;
    }
    const FString Name = PackageName.ToString();
    return Name.StartsWith(TEXT("/Script/")) || Name.StartsWith(TEXT("/Engine/"));
}

void CollectPackageLinks(
    const FName& PackageName,
    bool bReferencers,
    UE::AssetRegistry::EDependencyQuery QueryFlag,
    const TCHAR* TypeLabel,
    bool bIncludeEngine,
    TArray<TPair<FName, FString>>& OutLinks)
{
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    TArray<FName> Names;
    const UE::AssetRegistry::FDependencyQuery Query(QueryFlag);
    if (bReferencers)
    {
        Registry.GetReferencers(PackageName, Names, UE::AssetRegistry::EDependencyCategory::Package, Query);
    }
    else
    {
        Registry.GetDependencies(PackageName, Names, UE::AssetRegistry::EDependencyCategory::Package, Query);
    }
    Names.Sort(FNameLexicalLess());
    for (const FName& Name : Names)
    {
        if (!ShouldSkipPackage(Name, bIncludeEngine))
        {
            OutLinks.Emplace(Name, TypeLabel);
        }
    }
}

TSharedPtr<FJsonObject> HandleAssetPackageLinks(
    const FString& Operation,
    const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload,
    bool bReferencers)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
    }

    const FString PackageName = PackageNameFromAssetPath(AssetPath);
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    TArray<FAssetData> PackageAssets;
    Registry.GetAssetsByPackageName(FName(*PackageName), PackageAssets, true);
    if (PackageAssets.Num() == 0)
    {
        return MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), TEXT("No asset registry entry for the package"));
    }

    bool bIncludeSoft = true;
    Payload->TryGetBoolField(TEXT("include_soft"), bIncludeSoft);
    bool bIncludeEngine = false;
    Payload->TryGetBoolField(TEXT("include_engine"), bIncludeEngine);

    TArray<TPair<FName, FString>> Links;
    CollectPackageLinks(FName(*PackageName), bReferencers, UE::AssetRegistry::EDependencyQuery::Hard, TEXT("hard"), bIncludeEngine, Links);
    if (bIncludeSoft)
    {
        CollectPackageLinks(FName(*PackageName), bReferencers, UE::AssetRegistry::EDependencyQuery::Soft, TEXT("soft"), bIncludeEngine, Links);
    }

    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 200, 2000);
    TArray<TSharedPtr<FJsonValue>> Items;
    for (int32 Index = Offset; Index < Links.Num() && Items.Num() < Limit; ++Index)
    {
        TArray<TSharedPtr<FJsonValue>> Row;
        Row.Add(MakeShared<FJsonValueString>(Links[Index].Key.ToString()));
        Row.Add(MakeShared<FJsonValueString>(Links[Index].Value));
        Items.Add(MakeShared<FJsonValueArray>(Row));
    }
    const bool bHasMore = Offset + Items.Num() < Links.Num();

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("package_name"), PackageName);
    Data->SetStringField(TEXT("direction"), bReferencers ? TEXT("referencers") : TEXT("dependencies"));
    Data->SetArrayField(TEXT("columns"), {
        MakeShared<FJsonValueString>(TEXT("package_name")),
        MakeShared<FJsonValueString>(TEXT("type"))
    });
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetNumberField(TEXT("total"), Links.Num());
    Data->SetBoolField(TEXT("has_more"), bHasMore);
    if (bHasMore)
    {
        Data->SetStringField(TEXT("next_cursor"), FString::FromInt(Offset + Items.Num()));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}

TSharedPtr<FJsonObject> HandleAssetDependenciesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetPackageLinks(Operation, RequestId, Payload, false);
}

TSharedPtr<FJsonObject> HandleAssetReferencersGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetPackageLinks(Operation, RequestId, Payload, true);
}
}
