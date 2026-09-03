#include "UeNodeNexusBridgeTranscode.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;

static TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field)
{
    TArray<FString> Values;
    const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
    if (Payload->TryGetArrayField(Field, Items) && Items != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            FString Value;
            if (Item.IsValid() && Item->TryGetString(Value) && !Value.IsEmpty())
            {
                Values.Add(Value);
            }
        }
    }
    return Values;
}

TSharedPtr<FJsonObject> HandleTranscodeRootSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString Root;
    if (!Payload->TryGetStringField(TEXT("root"), Root) || Root.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("root is required"));
    }
    FString Error;
    if (!SetMirrorRoot(Root, Error))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_root"), Error);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("root"), GetMirrorRoot());
    Data->SetStringField(TEXT("schema_key"), SchemaKey());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static FString KindForAssetData(const FAssetData& AssetData)
{
    return KindForClass(AssetData.GetClass(EResolveClass::Yes));
}

static TSharedPtr<FJsonValue> StatusRow(const FAssetData& AssetData, const FString& Kind)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(AssetData.GetObjectPathString()));
    Row.Add(MakeShared<FJsonValueString>(AssetData.AssetClassPath.ToString()));
    Row.Add(MakeShared<FJsonValueString>(Kind));
    Row.Add(MakeShared<FJsonValueString>(PackageSavedHash(AssetData.PackageName.ToString())));
    Row.Add(MakeShared<FJsonValueBoolean>(IsPackageDirty(AssetData.PackageName.ToString())));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> HandleTranscodeStatus(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bDiscover = false;
    bool bIncludeStubs = false;
    Payload->TryGetBoolField(TEXT("discover"), bDiscover);
    Payload->TryGetBoolField(TEXT("include_stubs"), bIncludeStubs);
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();

    TArray<TSharedPtr<FJsonValue>> Rows;
    TSet<FString> Seen;
    for (const FString& AssetPath : ReadStringArray(Payload, TEXT("asset_paths")))
    {
        const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
        if (!AssetData.IsValid())
        {
            continue;
        }
        Seen.Add(AssetData.GetObjectPathString());
        Rows.Add(StatusRow(AssetData, KindForAssetData(AssetData)));
    }
    if (bDiscover)
    {
        TArray<FAssetData> Assets;
        Registry.GetAssetsByPath(FName(TEXT("/Game")), Assets, true);
        for (const FAssetData& AssetData : Assets)
        {
            if (Seen.Contains(AssetData.GetObjectPathString()))
            {
                continue;
            }
            const FString Kind = KindForAssetData(AssetData);
            if (Kind == TEXT("stub") && !bIncludeStubs)
            {
                continue;
            }
            Rows.Add(StatusRow(AssetData, Kind));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("columns"), { MakeShared<FJsonValueString>(TEXT("asset_path")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("kind")), MakeShared<FJsonValueString>(TEXT("saved_hash")), MakeShared<FJsonValueString>(TEXT("dirty")) });
    Data->SetArrayField(TEXT("assets"), Rows);
    Data->SetNumberField(TEXT("count"), Rows.Num());
    Data->SetStringField(TEXT("schema_key"), SchemaKey());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

// --- auto export on save --------------------------------------------------
static FDelegateHandle GWatchHandle;
static FString GWatchOutDir;

static void OnPackageSavedForWatch(const FString& /*Filename*/, UPackage* Package, FObjectPostSaveContext /*Context*/)
{
    if (Package == nullptr || GWatchOutDir.IsEmpty() || !Package->GetName().StartsWith(TEXT("/Game/")))
    {
        return;
    }
    UObject* Asset = Package->FindAssetInPackage();
    if (Asset == nullptr)
    {
        return;
    }
    const FString Kind = KindForClass(Asset->GetClass());
    TSharedPtr<FJsonObject> Raw = BuildRawForAsset(Asset, Kind);
    if (!Raw.IsValid())
    {
        return;
    }
    FString File;
    FString Error;
    if (ResolveRawFile(GWatchOutDir, Asset->GetPathName(), File, Error))
    {
        WriteJsonFile(File, Raw, Error);
    }
}

TSharedPtr<FJsonObject> HandleTranscodeWatchSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bEnabled = false;
    Payload->TryGetBoolField(TEXT("enabled"), bEnabled);
    FString OutDir;
    Payload->TryGetStringField(TEXT("out_dir"), OutDir);
    if (bEnabled && (OutDir.IsEmpty() || !IsInsideMirrorRoot(OutDir / TEXT("x"))))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("out_dir inside the registered mirror root is required to enable the watcher"));
    }
    if (GWatchHandle.IsValid())
    {
        UPackage::PackageSavedWithContextEvent.Remove(GWatchHandle);
        GWatchHandle.Reset();
    }
    GWatchOutDir.Reset();
    if (bEnabled)
    {
        GWatchOutDir = OutDir;
        GWatchHandle = UPackage::PackageSavedWithContextEvent.AddStatic(&OnPackageSavedForWatch);
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("enabled"), bEnabled);
    Data->SetStringField(TEXT("out_dir"), GWatchOutDir);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
