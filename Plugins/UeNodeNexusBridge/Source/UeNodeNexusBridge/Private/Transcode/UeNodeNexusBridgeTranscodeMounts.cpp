#include "UeNodeNexusBridgeTranscode.h"

#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace UeNodeNexusBridge::Transcode
{
bool IsProjectContentPackage(const FString& PackageName)
{
    if (!FPackageName::IsValidLongPackageName(PackageName))
    {
        return false;
    }
    FString Filename;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename))
    {
        return false;
    }
    // Mount registration alone also includes engine plugins and transient data.
    // Collaboration owns only content physically rooted in this project.
    return FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),
        FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
}

TArray<FString> ProjectContentRoots()
{
    TArray<FString> Roots;
    FPackageName::QueryRootContentPaths(Roots, false, false, true);
    Roots.RemoveAll([](const FString& Root)
    {
        return !IsProjectContentPackage(Root / TEXT("__NexusMountProbe__"));
    });
    Roots.Sort();
    return Roots;
}
}
