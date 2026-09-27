#include "NexusPackageState.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace UeNodeNexusBridge::Transcode
{
bool ReadPackageFileState(const FString& Filename, FPackageFileState& OutState)
{
    OutState = FPackageFileState();
#if PLATFORM_WINDOWS
    // UE 5.5 truncates its Windows file stamps to whole seconds for network
    // cooking. Saved-state and tag-cache keys need the native FILETIME value.
    const FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
    WIN32_FILE_ATTRIBUTE_DATA Data;
    if (!GetFileAttributesExW(*FullPath, GetFileExInfoStandard, &Data)
        || (Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        return false;
    }
    OutState.Modified = (static_cast<uint64>(Data.ftLastWriteTime.dwHighDateTime) << 32) | Data.ftLastWriteTime.dwLowDateTime;
    OutState.Size = static_cast<int64>((static_cast<uint64>(Data.nFileSizeHigh) << 32) | Data.nFileSizeLow);
#else
    const FFileStatData Data = IFileManager::Get().GetStatData(*Filename);
    if (!Data.bIsValid || Data.bIsDirectory)
    {
        return false;
    }
    OutState.Modified = static_cast<uint64>(Data.ModificationTime.GetTicks());
    OutState.Size = Data.FileSize;
#endif
    return true;
}
}
