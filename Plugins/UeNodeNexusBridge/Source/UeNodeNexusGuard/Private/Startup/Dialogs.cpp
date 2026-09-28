#include "Dialogs.h"
#include "../Control/State.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Text.h"
#include "Misc/CoreDelegates.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace NexusLifecycle::Dialogs
{
using FModalDialog = TDelegate<EAppReturnType::Type(EAppMsgCategory, EAppMsgType::Type, const FText&, const FText&)>;

struct FDialogs
{
    FModalDialog Original;
    FDelegateHandle Wrapped;
    bool bAcknowledge = false;
    int32 NextId = 1;
    // Guarded by State().Mutex, like every field a status snapshot reads.
    TArray<TSharedPtr<FJsonObject>> Open;
    TArray<TSharedPtr<FJsonObject>> Notices;
};

static FDialogs& Get()
{
    static FDialogs Value;
    return Value;
}

static const TCHAR* CategoryName(EAppMsgCategory Category)
{
    switch (Category)
    {
    case EAppMsgCategory::Error:
        return TEXT("error");
    case EAppMsgCategory::Success:
        return TEXT("success");
    case EAppMsgCategory::Info:
        return TEXT("info");
    default:
        return TEXT("warning");
    }
}

static const TCHAR* ButtonsName(EAppMsgType::Type Type)
{
    static const TCHAR* Names[] = {TEXT("ok"), TEXT("yes_no"), TEXT("ok_cancel"), TEXT("yes_no_cancel"), TEXT("cancel_retry_continue"),
        TEXT("yes_no_yes_all_no_all"), TEXT("yes_no_yes_all_no_all_cancel"), TEXT("yes_no_yes_all")};
    const int32 Index = static_cast<int32>(Type);
    return Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(Names)) ? Names[Index] : TEXT("unknown");
}

static void AddLowSpace(TArray<TSharedPtr<FJsonValue>>& Locations, const TCHAR* Role, const FString& Directory, uint64 RecommendedMb)
{
    uint64 Total = 0;
    uint64 Free = 0;
    if (!FPlatformMisc::GetDiskTotalAndFreeSpace(Directory, Total, Free) || Free / (1024 * 1024) >= RecommendedMb)
    {
        return;
    }
    auto Location = MakeShared<FJsonObject>();
    Location->SetStringField(TEXT("role"), Role);
    Location->SetStringField(TEXT("path"), FPaths::ConvertRelativePathToFull(Directory));
    Location->SetNumberField(TEXT("free_mb"), static_cast<double>(Free / (1024 * 1024)));
    Location->SetNumberField(TEXT("recommended_mb"), static_cast<double>(RecommendedMb));
    Location->SetNumberField(TEXT("total_mb"), static_cast<double>(Total / (1024 * 1024)));
    Locations.Add(MakeShared<FJsonValueObject>(Location));
}

// The dialog UUnrealEdEngine::ValidateFreeDiskSpace opens during editor init
// (UnrealEdEngine.cpp): an OK-only report that the engine and project
// directories hold less than 5120 MB, or the user settings directory less
// than 1024 MB. Its title is language independent through its text key.
static bool IsLowDriveSpace(const FText& Title)
{
    const TOptional<FString> Namespace = FTextInspector::GetNamespace(Title);
    const TOptional<FString> Key = FTextInspector::GetKey(Title);
    return Namespace.IsSet() && Key.IsSet() && *Namespace == TEXT("DriveSpaceDialog") && *Key == TEXT("LowHardDriveSpaceMsgTitle");
}

static TSharedPtr<FJsonObject> Describe(EAppMsgCategory Category, EAppMsgType::Type Type, const FText& Message, const FText& Title)
{
    auto Dialog = MakeShared<FJsonObject>();
    Dialog->SetStringField(TEXT("source"), TEXT("message_dialog"));
    Dialog->SetStringField(TEXT("title"), Title.ToString());
    Dialog->SetStringField(TEXT("message"), Message.ToString().Left(4096));
    Dialog->SetStringField(TEXT("category"), CategoryName(Category));
    Dialog->SetStringField(TEXT("buttons"), ButtonsName(Type));
    Dialog->SetStringField(TEXT("opened_at"), FDateTime::UtcNow().ToIso8601());
    if (IsLowDriveSpace(Title))
    {
        TArray<TSharedPtr<FJsonValue>> Locations;
        AddLowSpace(Locations, TEXT("engine"), FPlatformProcess::BaseDir(), 5120);
        AddLowSpace(Locations, TEXT("project"), FPlatformMisc::ProjectDir(), 5120);
        AddLowSpace(Locations, TEXT("user"), FPlatformProcess::UserDir(), 1024);
        Dialog->SetStringField(TEXT("code"), TEXT("low_drive_space"));
        Dialog->SetStringField(TEXT("meaning"), TEXT("free space is below UE's recommendation; confirming only continues startup"));
        Dialog->SetArrayField(TEXT("locations"), Locations);
    }
    return Dialog;
}

static void Remember(const TSharedPtr<FJsonObject>& Dialog, const TCHAR* Resolution, EAppReturnType::Type Result)
{
    Dialog->SetStringField(TEXT("resolution"), Resolution);
    Dialog->SetStringField(TEXT("result"), LexToString(Result));
    Dialog->SetStringField(TEXT("closed_at"), FDateTime::UtcNow().ToIso8601());
    TArray<TSharedPtr<FJsonObject>>& Notices = Get().Notices;
    Notices.Add(Dialog);
    if (Notices.Num() > 16)
    {
        Notices.RemoveAt(0);
    }
}

static EAppReturnType::Type Intercept(EAppMsgCategory Category, EAppMsgType::Type Type, const FText& Message, const FText& Title)
{
    FDialogs& D = Get();
    const TSharedPtr<FJsonObject> Dialog = Describe(Category, Type, Message, Title);
    // Only an advisory with a single answer is answered for the user: every
    // other prompt, including a real failure to write, waits and is reported.
    const bool bAcknowledge = D.bAcknowledge && Type == EAppMsgType::Ok && Dialog->HasField(TEXT("code"));
    int32 Id = 0;
    {
        FScopeLock Lock(&State().Mutex);
        Id = D.NextId++;
        Dialog->SetNumberField(TEXT("id"), Id);
        if (bAcknowledge)
        {
            Remember(Dialog, TEXT("auto_acknowledged"), EAppReturnType::Ok);
        }
        else
        {
            D.Open.Add(Dialog);
        }
    }
    if (bAcknowledge)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus Guard dialog auto_acknowledged id=%d title=\"%s\" message=\"%s\""), Id, *Title.ToString(), *Message.ToString());
        return EAppReturnType::Ok;
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard dialog waiting_for_user id=%d buttons=%s title=\"%s\""), Id, ButtonsName(Type), *Title.ToString());
    const EAppReturnType::Type Result = D.Original.IsBound()
        ? D.Original.Execute(Category, Type, Message, Title)
        : FPlatformMisc::MessageBoxExt(Type, *Message.ToString(), *Title.ToString());
    {
        FScopeLock Lock(&State().Mutex);
        D.Open.Remove(Dialog);
        Remember(Dialog, TEXT("answered"), Result);
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus Guard dialog answered id=%d result=%s"), Id, LexToString(Result));
    return Result;
}

void Install(bool bAcknowledge)
{
    Get().bAcknowledge = bAcknowledge;
    Wrap();
}

void Wrap()
{
    FDialogs& D = Get();
    // UEditorEngine::Init replaces the startup binding with its own handler; that
    // handler is captured once. A handler bound later may already forward to this
    // wrapper, so capturing it too could chain the two into each other.
    if (!IsInGameThread() || D.Original.IsBound()
        || (D.Wrapped.IsValid() && FCoreDelegates::ModalMessageDialog.GetHandle() == D.Wrapped))
    {
        return;
    }
    D.Original = FCoreDelegates::ModalMessageDialog;
    FCoreDelegates::ModalMessageDialog.BindStatic(&Intercept);
    D.Wrapped = FCoreDelegates::ModalMessageDialog.GetHandle();
}

void Uninstall()
{
    FDialogs& D = Get();
    if (D.Wrapped.IsValid() && FCoreDelegates::ModalMessageDialog.GetHandle() == D.Wrapped)
    {
        FCoreDelegates::ModalMessageDialog = D.Original;
    }
    D.Original.Unbind();
    D.Wrapped.Reset();
}

void AppendStatus(const TSharedPtr<FJsonObject>& Status)
{
    const FDialogs& D = Get();
    // Copies: the answering thread keeps writing the records after this lock.
    Status->SetBoolField(TEXT("waiting_for_user"), !D.Open.IsEmpty());
    if (!D.Open.IsEmpty())
    {
        Status->SetObjectField(TEXT("blocking_dialog"), MakeShared<FJsonObject>(*D.Open.Last()));
    }
    TArray<TSharedPtr<FJsonValue>> Notices;
    for (const TSharedPtr<FJsonObject>& Notice : D.Notices)
    {
        Notices.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>(*Notice)));
    }
    Status->SetArrayField(TEXT("dialog_notices"), Notices);
}

TSharedPtr<FJsonObject> OpenLocked()
{
    const FDialogs& D = Get();
    if (D.Open.IsEmpty())
    {
        return nullptr;
    }
    return MakeShared<FJsonObject>(*D.Open.Last());
}
}
