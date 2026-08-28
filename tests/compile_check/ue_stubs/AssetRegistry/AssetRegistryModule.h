// AssetRegistry stubs for compiler-only checks. Signatures mirror the
// documented UE 5.5 IAssetRegistry query surface; see CoreTypes.h.
#pragma once

#include "CoreMinimal.h"

struct FTopLevelAssetPath
{
    FName GetPackageName() const;
    FName GetAssetName() const;
};

struct FAssetData
{
    FName PackageName;
    FName AssetName;
    FTopLevelAssetPath AssetClassPath;
};

namespace UE::AssetRegistry
{
enum class EDependencyCategory : uint8
{
    Package = 1,
    Manage = 2,
    SearchableName = 4,
    All = Package | Manage | SearchableName,
};

enum class EDependencyQuery : uint32
{
    NoRequirements = 0,
    Hard = 1,
    Soft = 2,
    Game = 4,
    EditorOnly = 8,
    Propagation = 16,
    Direct = 32,
};

struct FDependencyQuery
{
    FDependencyQuery() = default;
    FDependencyQuery(EDependencyQuery InFlags);
};
}

class IAssetRegistry
{
public:
    virtual ~IAssetRegistry() = default;

    virtual bool GetDependencies(
        FName PackageName,
        TArray<FName>& OutDependencies,
        UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package,
        const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const = 0;

    virtual bool GetReferencers(
        FName PackageName,
        TArray<FName>& OutReferencers,
        UE::AssetRegistry::EDependencyCategory Category = UE::AssetRegistry::EDependencyCategory::Package,
        const UE::AssetRegistry::FDependencyQuery& Flags = UE::AssetRegistry::FDependencyQuery()) const = 0;

    virtual bool GetAssetsByPackageName(
        FName PackageName,
        TArray<FAssetData>& OutAssetData,
        bool bIncludeOnlyOnDiskAssets = false,
        bool bSkipARFilteredAssets = true) const = 0;
};

class FAssetRegistryModule
{
public:
    static IAssetRegistry& GetRegistry();
};
