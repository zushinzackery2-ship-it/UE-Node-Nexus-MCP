#include "Identity.h"
#include "PathCase.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <bcrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace NexusLifecycle
{
FString CanonicalPath(const FString& Path, bool bDirectory)
{
    const FString Absolute = FPaths::ConvertRelativePathToFull(Path);
    HANDLE File = CreateFileW(*Absolute, FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        bDirectory ? FILE_FLAG_BACKUP_SEMANTICS : 0, nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        return FString();
    }
    const DWORD Size = GetFinalPathNameByHandleW(File, nullptr, 0, FILE_NAME_NORMALIZED);
    TArray<WCHAR> Buffer;
    Buffer.SetNumZeroed(Size + 1);
    const bool bResolved = Size && GetFinalPathNameByHandleW(File, Buffer.GetData(), Size + 1, FILE_NAME_NORMALIZED);
    CloseHandle(File);
    if (!bResolved)
    {
        return FString();
    }
    FString Result(Buffer.GetData());
    if (Result.StartsWith(TEXT("\\\\?\\UNC\\")))
    {
        Result = TEXT("\\\\") + Result.Mid(8);
    }
    else
    {
        Result.RemoveFromStart(TEXT("\\\\?\\"));
    }
    return FoldPathCase(Result);
}

FString Sha256(const FString& Text)
{
    BCRYPT_ALG_HANDLE Algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    {
        return FString();
    }
    uint8 Digest[32];
    FTCHARToUTF8 Utf8(*Text);
    BCRYPT_HASH_HANDLE Hash = nullptr;
    NTSTATUS Status = BCryptCreateHash(Algorithm, &Hash, nullptr, 0, nullptr, 0, 0);
    if (Status == 0)
    {
        Status = BCryptHashData(Hash, reinterpret_cast<PUCHAR>(const_cast<ANSICHAR*>(Utf8.Get())), Utf8.Length(), 0);
        if (Status == 0)
        {
            Status = BCryptFinishHash(Hash, Digest, sizeof(Digest), 0);
        }
        BCryptDestroyHash(Hash);
    }
    BCryptCloseAlgorithmProvider(Algorithm, 0);
    return Status == 0 ? BytesToHex(Digest, sizeof(Digest)).ToLower() : FString();
}

FString DefaultRuntime()
{
    return FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA")) / TEXT("UE-Node-Nexus-MCP/Runtime");
}

FString Encode(const TSharedPtr<FJsonObject>& Object)
{
    FString Text;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
    return Text;
}

TSharedPtr<FJsonObject> ReadObject(const FString& Path)
{
    FString Text;
    TSharedPtr<FJsonObject> Result;
    if (FFileHelper::LoadFileToString(Text, *Path))
    {
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    }
    return Result;
}

bool WriteAtomic(const FString& Path, const TSharedPtr<FJsonObject>& Object)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    const FString Temporary = Path + TEXT(".tmp.") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if (!FFileHelper::SaveStringToFile(Encode(Object), *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return false;
    }
    return MoveFileExW(*Temporary, *Path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
}
