#include "AutoIndex/Query/NexusAutoIndexQuery.h"

#include "AutoIndex/State/NexusAutoIndexState.h"
#include "Dom/JsonObject.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeListFormats.h"

namespace UeNodeNexusBridge::AutoIndex
{
namespace
{
FString MakeStatusText()
{
    const FState& State = FState::Get();
    return FString::Printf(
        TEXT("S:enabled=%d|listening=%d|assets=%d|folders=%d|dirty=%d|serial=%d\nP:%s\nT:build=%s|flush=%s|event=%s\n"),
        State.bEnabled ? 1 : 0,
        State.IsListening() ? 1 : 0,
        State.AssetsByObjectPath.Num(),
        State.FoldersByPath.Num(),
        State.bDirty ? 1 : 0,
        State.ChangeSerial,
        *EscapeIndexedToken(State.IndexPath()),
        *EscapeIndexedToken(State.LastBuildUtc),
        *EscapeIndexedToken(State.LastFlushUtc),
        *EscapeIndexedToken(State.LastEventUtc));
}
}

TSharedPtr<FJsonObject> BuildStatusData(const FString& Format)
{
    const FState& State = FState::Get();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), Format);
    Data->SetBoolField(TEXT("enabled"), State.bEnabled);
    Data->SetBoolField(TEXT("listening"), State.IsListening());
    Data->SetBoolField(TEXT("dirty"), State.bDirty);
    Data->SetBoolField(TEXT("loaded_from_disk"), State.bLoadedFromDisk);
    Data->SetStringField(TEXT("index_path"), State.IndexPath());
    Data->SetStringField(TEXT("root_path"), State.RootPath);
    Data->SetStringField(TEXT("last_build_utc"), State.LastBuildUtc);
    Data->SetStringField(TEXT("last_flush_utc"), State.LastFlushUtc);
    Data->SetStringField(TEXT("last_event_utc"), State.LastEventUtc);
    Data->SetNumberField(TEXT("asset_count"), State.AssetsByObjectPath.Num());
    Data->SetNumberField(TEXT("folder_count"), State.FoldersByPath.Num());
    Data->SetNumberField(TEXT("change_serial"), State.ChangeSerial);
    SetTextPayload(Data, MakeStatusText());
    return Data;
}
}
