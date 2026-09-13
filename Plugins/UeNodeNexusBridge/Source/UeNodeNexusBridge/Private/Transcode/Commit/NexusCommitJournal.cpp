#include "NexusCommitInternal.h"

#include "UeNodeNexusBridgeTranscodeApi.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UeNodeNexusBridge::Collaboration
{
FString Text(const FJson& Json, const TCHAR* Key)
{
    FString Result;
    if (Json.IsValid())
    {
        Json->TryGetStringField(Key, Result);
    }
    return Result;
}

bool Flag(const FJson& Json, const TCHAR* Key, bool Default)
{
    if (Json.IsValid())
    {
        Json->TryGetBoolField(Key, Default);
    }
    return Default;
}

FJson Object(const FJson& Json, const TCHAR* Key)
{
    const FJson* Result = nullptr;
    return Json.IsValid() && Json->TryGetObjectField(Key, Result) ? *Result : MakeShared<FJsonObject>();
}

TArray<TSharedPtr<FJsonValue>> Rows(const FJson& Json, const TCHAR* Key)
{
    const TArray<TSharedPtr<FJsonValue>>* Result = nullptr;
    return Json.IsValid() && Json->TryGetArrayField(Key, Result) ? *Result : TArray<TSharedPtr<FJsonValue>>();
}

FString TransactionDirectory(const FString& ApplyId)
{
    if (ApplyId.Len() < 16 || ApplyId.Len() > 64 || !ApplyId.GetCharArray().FilterByPredicate([](TCHAR C)
    {
        return C != 0 && !FChar::IsAlnum(C) && C != TEXT('-');
    }).IsEmpty())
    {
        return FString();
    }
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Nexus/Collaboration") / ApplyId);
}

bool ReadJournal(const FString& File, FJson& Json)
{
    FString Data;
    return FFileHelper::LoadFileToString(Data, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Data), Json);
}

bool WriteJournal(const FString& File, const FJson& Json, FString& Error)
{
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FString Data;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Data);
    if (!FJsonSerializer::Serialize(Json.ToSharedRef(), Writer))
    {
        Error = TEXT("journal serialization failed");
        return false;
    }
    const FString Temporary = File + TEXT(".tmp");
    const FTCHARToUTF8 Bytes(*Data);
    TUniquePtr<FArchive> Stream(IFileManager::Get().CreateFileWriter(*Temporary));
    if (!Stream)
    {
        Error = TEXT("cannot create journal: ") + File;
        return false;
    }
    Stream->Serialize(const_cast<ANSICHAR*>(Bytes.Get()), Bytes.Length());
    Stream->Flush();
    const bool bOk = !Stream->IsError() && Stream->Close();
    Stream.Reset();
    if (!bOk || !IFileManager::Get().Move(*File, *Temporary, true, true))
    {
        Error = TEXT("cannot publish journal: ") + File;
        return false;
    }
    return true;
}

bool SaveReceipt(const FJson& Receipt, const FString& Phase, FString& Error)
{
    Receipt->SetStringField(TEXT("phase"), Phase);
    Receipt->SetStringField(TEXT("updated_at"), FDateTime::UtcNow().ToIso8601());
    UE_LOG(LogTemp, Display, TEXT("Nexus apply_id=%s phase=%s"), *Text(Receipt, TEXT("apply_id")), *Phase);
    return WriteJournal(TransactionDirectory(Text(Receipt, TEXT("apply_id"))) / TEXT("receipt.json"), Receipt, Error);
}

FString BoundRepository()
{
    FJson Binding;
    return ReadJournal(FPaths::ProjectSavedDir() / TEXT("Nexus/collaboration-binding.json"), Binding) ? Text(Binding, TEXT("repository")) : FString();
}

bool BindRepository(const FString& Repository, const FString& ProjectId, FString& Error)
{
    FString Normalized = FPaths::ConvertRelativePathToFull(Repository);
    FPaths::NormalizeDirectoryName(Normalized);
    const FString Existing = BoundRepository();
    FJson Previous;
    if (ReadJournal(FPaths::ProjectSavedDir() / TEXT("Nexus/collaboration-binding.json"), Previous)
        && Text(Previous, TEXT("project_id")) != ProjectId)
    {
        Error = TEXT("project_id does not match the existing collaboration binding");
        return false;
    }
    if (!Existing.IsEmpty() && !FPaths::IsSamePath(Existing, Normalized))
    {
        Error = TEXT("editor project is already bound to a different collaboration repository: ") + Existing;
        return false;
    }
    const FJson Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("repository"), Normalized);
    Binding->SetStringField(TEXT("project_id"), ProjectId);
    return WriteJournal(FPaths::ProjectSavedDir() / TEXT("Nexus/collaboration-binding.json"), Binding, Error);
}

bool HasPending(const FJson& Request, const FString& ApplyId, FString& Error)
{
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectSavedDir() / TEXT("Nexus/Collaboration")), TEXT("receipt.json"), true, false);
    for (const FString& File : Files)
    {
        FJson Receipt;
        if (ReadJournal(File, Receipt) && Text(Receipt, TEXT("apply_id")) != ApplyId
            && Text(Object(Receipt, TEXT("request")), TEXT("asset_path")) == Text(Request, TEXT("asset_path")))
        {
            const FString Phase = Text(Receipt, TEXT("phase"));
            if (Phase != TEXT("ue_committed") && Phase != TEXT("rolled_back") && Phase != TEXT("rejected"))
            {
                Error = TEXT("recover pending apply_id ") + Text(Receipt, TEXT("apply_id"));
                return true;
            }
        }
    }
    return false;
}
}
