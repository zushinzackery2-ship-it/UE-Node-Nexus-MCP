#include "NexusPackageFiles.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Collaboration
{
bool SaveStagedPackage(UPackage* Package, const FJson& Receipt, const FJson& Row, FString& Error)
{
    const FString Directory = TransactionDirectory(Text(Receipt, TEXT("apply_id"))) / TEXT("packages")
        / FMD5::HashAnsiString(*Package->GetName()) / TEXT("after");
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Output = Directory / FPaths::GetCleanFilename(Text(Row, TEXT("file")));
    if (!Transcode::SavePackageTo(Package, nullptr, Output, true, Error))
    {
        return false;
    }
    for (const auto& Value : Rows(Row, TEXT("files")))
    {
        const FJson File = Value->AsObject();
        const FString Source = Directory / FPaths::GetCleanFilename(Text(File, TEXT("path")));
        File->SetStringField(TEXT("planned_file"), Source);
        File->SetStringField(TEXT("planned_hash"), FileHash(Source));
    }
    if (!SaveReceipt(Receipt, TEXT("saving"), Error))
    {
        return false;
    }
    for (const auto& Value : Rows(Row, TEXT("files")))
    {
        const FJson File = Value->AsObject();
        const FString Path = Text(File, TEXT("path"));
        File->SetStringField(TEXT("save_phase"), TEXT("writing"));
        if (!SaveReceipt(Receipt, TEXT("saving"), Error))
        {
            return false;
        }
        const bool bAbsent = Text(File, TEXT("planned_hash")) == TEXT("absent");
        const bool bOk = bAbsent ? !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path, false, false, true)
            : CopyFile(Text(File, TEXT("planned_file")), Path, Error);
        if (!bOk)
        {
            return false;
        }
        File->SetStringField(TEXT("saved_hash"), FileHash(Path));
        File->SetStringField(TEXT("save_phase"), TEXT("saved"));
        if (!SaveReceipt(Receipt, TEXT("saving"), Error))
        {
            return false;
        }
    }
    Package->SetDirtyFlag(false);
    return true;
}
}
