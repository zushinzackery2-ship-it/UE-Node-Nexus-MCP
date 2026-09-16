#include "PathCase.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
static FString Lower(const FString& Value)
{
    const int Size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, *Value, -1, nullptr, 0, nullptr, nullptr, 0);
    if (!Size)
    {
        return FString();
    }
    TArray<WCHAR> Buffer;
    Buffer.SetNumZeroed(Size);
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, *Value, -1, Buffer.GetData(), Size, nullptr, nullptr, 0))
    {
        return FString();
    }
    return FString(Buffer.GetData());
}

FString FoldPathCase(FString Path)
{
    Path.ReplaceInline(TEXT("\\"), TEXT("/"));
    int32 Start = 3;
    if (Path.StartsWith(TEXT("//")))
    {
        const int32 ServerEnd = Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 2);
        const int32 ShareEnd = ServerEnd == INDEX_NONE ? INDEX_NONE
            : Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ServerEnd + 1);
        Start = ShareEnd == INDEX_NONE ? Path.Len() : ShareEnd + 1;
    }
    FString Result = Lower(Path.Left(Start));
    while (Start < Path.Len())
    {
        int32 End = Path.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
        if (End == INDEX_NONE)
        {
            End = Path.Len();
        }
        const FString Parent = Path.Left(Start);
        HANDLE Directory = CreateFileW(*Parent, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (Directory == INVALID_HANDLE_VALUE)
        {
            return FString();
        }
        ULONG Flags = 0;
        const bool bQueried = GetFileInformationByHandleEx(Directory, static_cast<FILE_INFO_BY_HANDLE_CLASS>(23), &Flags, sizeof(Flags)) != 0;
        const DWORD Error = bQueried ? ERROR_SUCCESS : GetLastError();
        CloseHandle(Directory);
        if (!bQueried && Error != ERROR_INVALID_FUNCTION && Error != ERROR_NOT_SUPPORTED && Error != ERROR_INVALID_PARAMETER)
        {
            return FString();
        }
        const FString Name = Path.Mid(Start, End - Start);
        const FString Folded = (Flags & 1) ? Name : Lower(Name);
        if (Folded.IsEmpty())
        {
            return FString();
        }
        Result += Folded;
        if (End < Path.Len())
        {
            Result += TEXT("/");
        }
        Start = End + 1;
    }
    return Result;
}
}
