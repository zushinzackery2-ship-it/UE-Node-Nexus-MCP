#include "UeNodeNexusBridgeAssetPaths.h"

#include "Misc/PackageName.h"

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

FString NormalizeAssetObjectPath(const FString& AssetPath)
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
}
