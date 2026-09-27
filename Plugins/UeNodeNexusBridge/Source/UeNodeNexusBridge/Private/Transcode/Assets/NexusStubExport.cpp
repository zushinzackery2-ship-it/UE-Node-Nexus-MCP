#include "Transcode/UeNodeNexusBridgeTranscode.h"

#include "NexusPackageState.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/LruCache.h"
#include "Misc/PackageName.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
struct FPackageTags
{
    FString Filename;
    FPackageFileState State;
    TArray<FAssetData> Assets;
};

TLruCache<FName, FPackageTags> SavedPackages(1024);

bool ReadSavedAsset(const FAssetData& AssetData, FAssetData& Saved, FString& Error)
{
    // All bridge exports run on the game thread; the cache holds metadata only.
    check(IsInGameThread());
    FString Filename;
    if (!FPackageName::DoesPackageExist(AssetData.PackageName.ToString(), &Filename))
    {
        SavedPackages.Remove(AssetData.PackageName);
        return true;
    }
    FPackageFileState State;
    if (!ReadPackageFileState(Filename, State))
    {
        SavedPackages.Remove(AssetData.PackageName);
        Error = TEXT("cannot stat saved package: ") + Filename;
        return false;
    }
    const FPackageTags* Cached = SavedPackages.FindAndTouch(AssetData.PackageName);
    if (!Cached || Cached->Filename != Filename || !(State == Cached->State))
    {
        // DiskGatheredData is updated from loaded objects by UE. Read the package
        // itself so loading/compiling an unchanged asset cannot rewrite its stub.
        IAssetRegistry::FLoadPackageRegistryData Data(false);
        FAssetRegistryModule::GetRegistry().LoadPackageRegistryData(Filename, Data);
        FPackageFileState After;
        if (!ReadPackageFileState(Filename, After) || !(State == After))
        {
            SavedPackages.Remove(AssetData.PackageName);
            Error = TEXT("saved package changed while reading tags: ") + Filename;
            return false;
        }
        FPackageTags Entry;
        Entry.Filename = Filename;
        Entry.State = State;
        Entry.Assets = MoveTemp(Data.Data);
        SavedPackages.Add(AssetData.PackageName, Entry);
        Cached = SavedPackages.Find(AssetData.PackageName);
    }
    const FAssetData* Match = Cached->Assets.FindByPredicate([&AssetData](const FAssetData& Candidate)
    {
        return Candidate.GetSoftObjectPath() == AssetData.GetSoftObjectPath();
    });
    if (!Match)
    {
        SavedPackages.Remove(AssetData.PackageName);
        Error = FString::Printf(TEXT("saved asset metadata is missing or unreadable: %s in %s"),
            *AssetData.GetObjectPathString(), *Filename);
        return false;
    }
    Saved = *Match;
    return true;
}
}

TSharedPtr<FJsonObject> BuildStubRaw(const FAssetData& AssetData, FString* OutError)
{
    FAssetData Saved;
    FString Error;
    if (OutError)
    {
        OutError->Reset();
    }
    if (!ReadSavedAsset(AssetData, Saved, Error))
    {
        if (OutError)
        {
            *OutError = Error;
        }
        UE_LOG(LogTemp, Warning, TEXT("Nexus phase=stub_tags asset=%s error=%s"), *AssetData.GetObjectPathString(), *Error);
        return nullptr;
    }
    const TSharedPtr<FJsonObject> Raw = MakeShared<FJsonObject>();
    Raw->SetNumberField(TEXT("raw_version"), 1);
    Raw->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
    Raw->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
    Raw->SetStringField(TEXT("class_short"), AssetData.AssetClassPath.GetAssetName().ToString());
    Raw->SetStringField(TEXT("kind"), TEXT("stub"));
    Raw->SetStringField(TEXT("schema_key"), SchemaKey());
    Raw->SetStringField(TEXT("saved_hash"), PackageSavedHash(AssetData.PackageName.ToString()));
    Raw->SetBoolField(TEXT("dirty"), IsPackageDirty(AssetData.PackageName.ToString()));
    Raw->SetArrayField(TEXT("props"), TArray<TSharedPtr<FJsonValue>>());
    TArray<TSharedPtr<FJsonValue>> Tags;
    Saved.EnumerateTags([&Tags](TPair<FName, FAssetTagValueRef> Pair)
    {
        const TSharedPtr<FJsonObject> Tag = MakeShared<FJsonObject>();
        Tag->SetStringField(TEXT("name"), Pair.Key.ToString());
        Tag->SetStringField(TEXT("value"), Pair.Value.AsString());
        Tags.Add(MakeShared<FJsonValueObject>(Tag));
    });
    Tags.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
    {
        return Left->AsObject()->GetStringField(TEXT("name")) < Right->AsObject()->GetStringField(TEXT("name"));
    });
    Raw->SetArrayField(TEXT("tags"), Tags);
    return Raw;
}
}
