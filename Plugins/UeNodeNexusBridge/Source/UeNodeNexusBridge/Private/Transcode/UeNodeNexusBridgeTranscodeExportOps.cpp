#include "UeNodeNexusBridgeTranscode.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/SoftObjectPath.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;

namespace Transcode
{
TSharedPtr<FJsonObject> BuildGenericRaw(UObject* Asset)
{
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Asset, TEXT("asset"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Asset));
    return Raw;
}

TSharedPtr<FJsonObject> BuildStubRaw(const FAssetData& AssetData)
{
    TSharedPtr<FJsonObject> Raw = MakeShared<FJsonObject>();
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
    AssetData.EnumerateTags([&Tags](TPair<FName, FAssetTagValueRef> Pair)
    {
        TSharedPtr<FJsonObject> Tag = MakeShared<FJsonObject>();
        Tag->SetStringField(TEXT("name"), Pair.Key.ToString());
        Tag->SetStringField(TEXT("value"), Pair.Value.AsString());
        Tags.Add(MakeShared<FJsonValueObject>(Tag));
    });
    Raw->SetArrayField(TEXT("tags"), Tags);
    return Raw;
}

TSharedPtr<FJsonObject> BuildRawForAsset(UObject* Asset, const FString& Kind)
{
    if (Asset == nullptr)
    {
        return nullptr;
    }
    if (Kind == TEXT("material"))
    {
        return BuildMaterialRaw(Cast<UMaterial>(Asset));
    }
    if (Kind == TEXT("material_function"))
    {
        return BuildMaterialFunctionRaw(Cast<UMaterialFunction>(Asset));
    }
    if (Kind == TEXT("material_instance"))
    {
        return BuildMaterialInstanceRaw(Cast<UMaterialInstanceConstant>(Asset));
    }
    if (Kind == TEXT("blueprint"))
    {
        return BuildBlueprintRaw(Cast<UBlueprint>(Asset));
    }
    if (Kind == TEXT("asset"))
    {
        return BuildGenericRaw(Asset);
    }
    return nullptr;
}
}

static TSharedPtr<FJsonObject> ExportRow(const FString& AssetPath, const FString& ClassPath, const FString& Kind, const FString& File, const TSharedPtr<FJsonObject>& Raw)
{
    TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("asset_path"), AssetPath);
    Row->SetStringField(TEXT("class"), ClassPath);
    Row->SetStringField(TEXT("kind"), Kind);
    Row->SetStringField(TEXT("file"), File);
    Row->SetStringField(TEXT("saved_hash"), Raw.IsValid() ? Raw->GetStringField(TEXT("saved_hash")) : FString());
    Row->SetBoolField(TEXT("dirty"), Raw.IsValid() && Raw->GetBoolField(TEXT("dirty")));
    return Row;
}

TSharedPtr<FJsonObject> HandleTranscodeExport(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString OutDir;
    const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
    if (!Payload->TryGetStringField(TEXT("out_dir"), OutDir) || OutDir.IsEmpty() || !Payload->TryGetArrayField(TEXT("asset_paths"), Paths) || Paths == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_paths (array) and out_dir are required"));
    }
    if (GetMirrorRoot().IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("root_not_set"), TEXT("call transcode_root_set before exporting"));
    }
    bool bIncludeStubs = false;
    Payload->TryGetBoolField(TEXT("include_stubs"), bIncludeStubs);
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();

    TArray<TSharedPtr<FJsonValue>> Rows;
    TArray<TSharedPtr<FJsonValue>> Skipped;
    for (const TSharedPtr<FJsonValue>& Value : *Paths)
    {
        FString AssetPath;
        if (!Value.IsValid() || !Value->TryGetString(AssetPath) || AssetPath.IsEmpty())
        {
            continue;
        }
        const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
        UObject* Asset = AssetData.IsValid() ? nullptr : LoadObject<UObject>(nullptr, *AssetPath);
        FString Kind = AssetData.IsValid() ? KindForClass(AssetData.GetClass(EResolveClass::Yes)) : (Asset ? KindForClass(Asset->GetClass()) : TEXT("stub"));
        if (!AssetData.IsValid() && Asset == nullptr)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("asset_path"), AssetPath);
            Item->SetStringField(TEXT("reason"), TEXT("asset_not_found"));
            Skipped.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }
        TSharedPtr<FJsonObject> Raw;
        if (Kind == TEXT("stub"))
        {
            if (!bIncludeStubs || !AssetData.IsValid())
            {
                continue;
            }
            Raw = BuildStubRaw(AssetData);
        }
        else
        {
            if (Asset == nullptr)
            {
                Asset = AssetData.GetAsset();
            }
            Raw = BuildRawForAsset(Asset, Kind);
        }
        if (!Raw.IsValid())
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("asset_path"), AssetPath);
            Item->SetStringField(TEXT("reason"), Kind.StartsWith(TEXT("niagara")) ? TEXT("use_vfx_transcode_export") : TEXT("unsupported_kind"));
            Skipped.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }
        const FString ObjectPath = Raw->GetStringField(TEXT("asset_path"));
        FString File;
        FString Error;
        if (!ResolveRawFile(OutDir, ObjectPath, File, Error) || !WriteJsonFile(File, Raw, Error))
        {
            return MakeOperationError(Operation, RequestId, TEXT("write_failed"), Error);
        }
        Rows.Add(MakeShared<FJsonValueObject>(ExportRow(ObjectPath, Raw->GetStringField(TEXT("class")), Kind, File, Raw)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetArrayField(TEXT("assets"), Rows);
    Data->SetArrayField(TEXT("skipped"), Skipped);
    Data->SetNumberField(TEXT("count"), Rows.Num());
    Data->SetStringField(TEXT("schema_key"), SchemaKey());
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
