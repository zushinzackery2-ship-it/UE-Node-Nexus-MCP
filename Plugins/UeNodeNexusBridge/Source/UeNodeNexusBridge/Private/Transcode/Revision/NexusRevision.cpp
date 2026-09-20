#include "UeNodeNexusCollaboration.h"

#include "Level/Scene/NexusSceneWorld.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge::Collaboration
{
namespace
{
TMap<FString, FObserver> Observers;

FString Quote(const FString& Text)
{
    FString Result;
    const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
    Writer->WriteValue(Text);
    Writer->Close();
    return Result;
}

FString CanonicalValue(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid() || Value->Type == EJson::Null)
    {
        return TEXT("null");
    }
    if (Value->Type == EJson::Object)
    {
        TArray<FString> Keys;
        Value->AsObject()->Values.GetKeys(Keys);
        Keys.Sort();
        TArray<FString> Fields;
        for (const FString& Key : Keys)
        {
            Fields.Add(Quote(Key) + TEXT(":") + CanonicalValue(Value->AsObject()->Values[Key]));
        }
        return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
    }
    if (Value->Type == EJson::Array)
    {
        TArray<FString> Values;
        for (const auto& Item : Value->AsArray())
        {
            Values.Add(CanonicalValue(Item));
        }
        return TEXT("[") + FString::Join(Values, TEXT(",")) + TEXT("]");
    }
    if (Value->Type == EJson::String)
    {
        return Quote(Value->AsString());
    }
    if (Value->Type == EJson::Boolean)
    {
        return Value->AsBool() ? TEXT("true") : TEXT("false");
    }
    return FString::Printf(TEXT("%.17g"), Value->AsNumber());
}
}

FString EditorEpoch()
{
    static const FString Epoch = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    return Epoch;
}

FString ContentDigest(const FJson& Json)
{
    const FString Canonical = CanonicalValue(MakeShared<FJsonValueObject>(Json));
    const FTCHARToUTF8 Bytes(*Canonical);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
}

FJson StampRaw(const FJson& Raw)
{
    if (!Raw.IsValid())
    {
        return Raw;
    }
    const FJson Stable = MakeShared<FJsonObject>(*Raw);
    for (const TCHAR* Key : { TEXT("dirty"), TEXT("saved_hash"), TEXT("exported_at"), TEXT("generated_at"), TEXT("revision"), TEXT("live_revision"), TEXT("content_revision"), TEXT("editor_epoch"), TEXT("request_token"), TEXT("apply_id") })
    {
        Stable->RemoveField(Key);
    }
    Stable->SetStringField(TEXT("environment"), Transcode::SchemaKey());
    const FString Content = ContentDigest(Stable);
    const FString Revision = EditorEpoch() + TEXT(":") + Content;
    Raw->SetStringField(TEXT("content_revision"), Content);
    Raw->SetStringField(TEXT("live_revision"), Revision);
    Raw->SetStringField(TEXT("editor_epoch"), EditorEpoch());
    if (!Raw->HasField(TEXT("revision")))
    {
        Raw->SetStringField(TEXT("revision"), Revision);
    }
    return Raw;
}

void RegisterObserver(const FString& Kind, FObserver Observer)
{
    Observers.Add(Kind, MoveTemp(Observer));
}

void UnregisterObserver(const FString& Kind)
{
    Observers.Remove(Kind);
}

FJson Observe(const FJson& Request)
{
    FString Kind, AssetPath;
    Request->TryGetStringField(TEXT("kind"), Kind);
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (FObserver* Provider = Observers.Find(Kind))
    {
        return StampRaw((*Provider)(Request));
    }
    if (Kind == TEXT("scene"))
    {
        const FJson* Selector = nullptr;
        if (!Request->TryGetObjectField(TEXT("selector"), Selector))
        {
            return nullptr;
        }
        FString Error;
        UWorld* World = Scene::ResolveWorld(Scene::String(*Selector, TEXT("map_path")), Error);
        return World ? StampRaw(Scene::ExportScene(World, *Selector, Error)) : nullptr;
    }
    if (Kind == TEXT("stub"))
    {
        const FAssetData AssetData = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
        if (AssetData.IsValid())
        {
            return StampRaw(Transcode::BuildStubRaw(AssetData));
        }
    }
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        const FJson Missing = MakeShared<FJsonObject>();
        Missing->SetBoolField(TEXT("exists"), false);
        Missing->SetStringField(TEXT("asset_path"), AssetPath);
        return StampRaw(Missing);
    }
    return StampRaw(Transcode::BuildRawForAsset(Asset, Transcode::KindForClass(Asset->GetClass())));
}
}
