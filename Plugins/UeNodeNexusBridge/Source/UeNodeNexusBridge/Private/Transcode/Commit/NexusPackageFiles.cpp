#include "NexusPackageFiles.h"

#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "RenderAssetUpdate.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Collaboration
{
FString PackageFile(UPackage* Package)
{
    return FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package->GetName(),
        Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()));
}

FString FileHash(const FString& File)
{
    return IFileManager::Get().FileExists(*File) ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("absent");
}

bool CopyFile(const FString& Source, const FString& Target, FString& Error)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Target), true);
    const FString Temporary = Target + TEXT(".nexus-restore");
    if (IFileManager::Get().Copy(*Temporary, *Source, true, true) != COPY_OK
        || !IFileManager::Get().Move(*Target, *Temporary, true, true))
    {
        Error = TEXT("checkpoint copy failed: ") + Source + TEXT(" -> ") + Target;
        return false;
    }
    return true;
}

bool CapturePackage(UPackage* Package, const FJson& Receipt, FString& Error)
{
    if (IsAssetStreamingSuspended())
    {
        Error = TEXT("asset streaming is suspended during checkpoint");
        return false;
    }
    const FString File = PackageFile(Package);
    if (IFileManager::Get().FileExists(*File) && IFileManager::Get().IsReadOnly(*File))
    {
        Error = TEXT("read-only on disk: ") + File;
        return false;
    }
    const FString Root = TransactionDirectory(Text(Receipt, TEXT("apply_id"))) / TEXT("packages")
        / FMD5::HashAnsiString(*Package->GetName());
    const FString Memory = Root / TEXT("memory") / FPaths::GetCleanFilename(File);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Memory), true);
    const bool bDirty = Package->IsDirty();
    if (!Transcode::SavePackageTo(Package, nullptr, Memory, true, Error))
    {
        Error = TEXT("cannot serialize in-memory package checkpoint: ") + Package->GetName();
        return false;
    }
    Package->SetDirtyFlag(bDirty);
    FJson Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), Package->GetName());
    Row->SetStringField(TEXT("file"), File);
    Row->SetBoolField(TEXT("was_dirty"), bDirty);
    Row->SetBoolField(TEXT("existed"), true);
    Row->SetBoolField(TEXT("map"), Package->ContainsMap());
    TArray<TSharedPtr<FJsonValue>> Files;
    const TArray<FString> Extensions = { FPaths::GetExtension(File, true), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl"), TEXT(".m.ubulk") };
    for (const FString& Extension : Extensions)
    {
        const FString Original = FPaths::ChangeExtension(File, Extension);
        const FString Saved = Root / TEXT("disk") / FPaths::GetCleanFilename(Original);
        const FString Serialized = FPaths::ChangeExtension(Memory, Extension);
        FJson Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Original);
        Item->SetStringField(TEXT("disk"), Saved);
        Item->SetStringField(TEXT("memory"), Serialized);
        Item->SetStringField(TEXT("disk_hash"), FileHash(Original));
        Item->SetStringField(TEXT("memory_hash"), FileHash(Serialized));
        if (FileHash(Original) != TEXT("absent") && !CopyFile(Original, Saved, Error))
        {
            return false;
        }
        Files.Add(MakeShared<FJsonValueObject>(Item));
    }
    Row->SetArrayField(TEXT("files"), Files);
    auto Packages = Rows(Receipt, TEXT("packages"));
    Packages.Add(MakeShared<FJsonValueObject>(Row));
    Receipt->SetArrayField(TEXT("packages"), Packages);
    return SaveReceipt(Receipt, TEXT("prepared"), Error);
}

bool RestorePackageFiles(const FJson& Package, bool bMemory, FString& Error)
{
    for (const auto& Value : Rows(Package, TEXT("files")))
    {
        const FJson Item = Value->AsObject();
        const FString Path = Text(Item, TEXT("path"));
        const FString Hash = Text(Item, bMemory ? TEXT("memory_hash") : TEXT("disk_hash"));
        const FString Source = Text(Item, bMemory ? TEXT("memory") : TEXT("disk"));
        if (Hash == TEXT("absent"))
        {
            if (IFileManager::Get().FileExists(*Path) && !IFileManager::Get().Delete(*Path, false, false, true))
            {
                Error = TEXT("cannot restore absent file: ") + Path;
                return false;
            }
        }
        else if (FileHash(Source) != Hash)
        {
            Error = TEXT("checkpoint is corrupt: ") + Source;
            return false;
        }
        else if (!CopyFile(Source, Path, Error))
        {
            Error = TEXT("checkpoint cannot be restored: ") + Source + TEXT(" ") + Error;
            return false;
        }
    }
    return true;
}

bool CheckRecoveryFiles(const FJson& Receipt, FString& Error)
{
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        for (const auto& FileValue : Rows(Value->AsObject(), TEXT("files")))
        {
            const FJson File = FileValue->AsObject();
            const FString Current = FileHash(Text(File, TEXT("path")));
            if (Current != Text(File, TEXT("disk_hash")) && Current != Text(File, TEXT("saved_hash"))
                && Current != Text(File, TEXT("memory_hash")) && Current != Text(File, TEXT("planned_hash")))
            {
                Error = TEXT("disk changed outside the recorded apply: ") + Text(File, TEXT("path"));
                return false;
            }
        }
    }
    return true;
}
}
