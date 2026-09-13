#include "UeNodeNexusBridgeTranscode.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Curves/CurveBase.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace UeNodeNexusBridge::Transcode
{
static FString GMirrorRoot;

static FString NormalizeDir(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::MakeStandardFilename(Path);
    if (!Path.EndsWith(TEXT("/")))
    {
        Path += TEXT("/");
    }
    return Path;
}

static FString NormalizeFile(FString Path)
{
    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::MakeStandardFilename(Path);
    return Path;
}

bool SetMirrorRoot(const FString& Root, FString& OutError)
{
    if (Root.IsEmpty() || FPaths::IsRelative(Root))
    {
        OutError = TEXT("root must be an absolute path");
        return false;
    }
    const FString Normalized = NormalizeDir(Root);
    if (!IFileManager::Get().DirectoryExists(*Normalized))
    {
        OutError = FString::Printf(TEXT("root directory does not exist: %s"), *Normalized);
        return false;
    }
    if (Normalized.StartsWith(NormalizeDir(FPaths::EngineDir()), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("root must not be inside the Engine directory");
        return false;
    }
    const FString ContentDir = NormalizeDir(FPaths::ProjectContentDir());
    if (Normalized.StartsWith(ContentDir, ESearchCase::IgnoreCase) || ContentDir.StartsWith(Normalized, ESearchCase::IgnoreCase))
    {
        OutError = TEXT("root must not contain or live inside the project Content directory");
        return false;
    }
    GMirrorRoot = Normalized;
    return true;
}

FString GetMirrorRoot()
{
    return GMirrorRoot;
}

bool IsInsideMirrorRoot(const FString& Path)
{
    return !GMirrorRoot.IsEmpty() && NormalizeFile(Path).StartsWith(GMirrorRoot, ESearchCase::IgnoreCase);
}

bool ResolveRawFile(const FString& OutDir, const FString& AssetPath, FString& OutFile, FString& OutError)
{
    if (GMirrorRoot.IsEmpty())
    {
        OutError = TEXT("transcode_root_set has not been called in this editor session");
        return false;
    }
    FString Package = AssetPath;
    int32 DotIndex = INDEX_NONE;
    if (Package.FindLastChar(TEXT('.'), DotIndex) && DotIndex > Package.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd))
    {
        Package.LeftInline(DotIndex);
    }
    if (!Package.StartsWith(TEXT("/Game/")))
    {
        OutError = FString::Printf(TEXT("only /Game assets are mirrored: %s"), *AssetPath);
        return false;
    }
    OutFile = NormalizeFile(OutDir / Package.RightChop(6) + TEXT(".json"));
    if (!IsInsideMirrorRoot(OutFile))
    {
        OutError = FString::Printf(TEXT("out_dir is outside the registered mirror root: %s"), *OutDir);
        return false;
    }
    return true;
}

bool WriteJsonFile(const FString& File, const TSharedPtr<FJsonObject>& Json, FString& OutError)
{
    if (!IsInsideMirrorRoot(File))
    {
        OutError = FString::Printf(TEXT("refusing to write outside the mirror root: %s"), *File);
        return false;
    }
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FString Text;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("could not write %s"), *File);
        return false;
    }
    return true;
}

FString ShortClassName(const UClass* Class)
{
    return Class ? Class->GetName() : FString();
}

bool IsGenericAssetClass(const UClass* Class)
{
    if (Class == nullptr)
    {
        return false;
    }
    if (Class->IsChildOf(UDataAsset::StaticClass()) || Class->IsChildOf(UPhysicalMaterial::StaticClass())
        || Class->IsChildOf(UMaterialParameterCollection::StaticClass()) || Class->IsChildOf(UCurveBase::StaticClass()))
    {
        return true;
    }
    const FString Name = Class->GetName();
    return Name == TEXT("InputAction") || Name == TEXT("InputMappingContext");
}

FString KindForClass(const UClass* Class)
{
    if (Class == nullptr)
    {
        return TEXT("stub");
    }
    if (Class->IsChildOf(UMaterialFunctionMaterialLayer::StaticClass()) || Class->IsChildOf(UMaterialFunctionMaterialLayerBlend::StaticClass()))
    {
        return TEXT("stub");
    }
    if (Class->IsChildOf(UMaterialFunction::StaticClass()))
    {
        return TEXT("material_function");
    }
    if (Class->IsChildOf(UMaterial::StaticClass()))
    {
        return TEXT("material");
    }
    if (Class->IsChildOf(UMaterialInstanceConstant::StaticClass()))
    {
        return TEXT("material_instance");
    }
    if (Class == UBlueprint::StaticClass())
    {
        return TEXT("blueprint");
    }
    const FString Name = Class->GetName();
    if (Name == TEXT("NiagaraSystem"))
    {
        return TEXT("niagara_system");
    }
    if (Name == TEXT("NiagaraEmitter"))
    {
        return TEXT("niagara_emitter");
    }
    if (IsGenericAssetClass(Class))
    {
        return TEXT("asset");
    }
    return TEXT("stub");
}

FString EngineVersionString()
{
    return FEngineVersion::Current().ToString(EVersionComponent::Patch);
}

FString PackageSavedHash(const FString& PackageName)
{
    // The on-disk stamp updates the moment SavePackage returns; the AssetRegistry's
    // saved hash lags behind (async rescan), which would read as a phantom change.
    FString Filename;
    if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename, FPackageName::GetAssetPackageExtension()))
    {
        const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Filename);
        if (Stamp != FDateTime::MinValue())
        {
            return FString::Printf(TEXT("mtime:%s:%lld"), *Stamp.ToIso8601(), IFileManager::Get().FileSize(*Filename));
        }
    }
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const TOptional<FAssetPackageData> Data = Registry.GetAssetPackageDataCopy(FName(*PackageName));
    if (Data.IsSet())
    {
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
        return LexToString(Data->GetPackageSavedHash());
#else
        return Data->PackageGuid.ToString(EGuidFormats::DigitsWithHyphens);
#endif
    }
    return FString();
}

bool IsPackageDirty(const FString& PackageName)
{
    UPackage* Package = FindPackage(nullptr, *PackageName);
    return Package != nullptr && Package->IsDirty();
}

TSharedPtr<FJsonObject> MakeRawEnvelope(UObject* Asset, const FString& Kind)
{
    TSharedPtr<FJsonObject> Raw = MakeShared<FJsonObject>();
    Raw->SetNumberField(TEXT("raw_version"), 1);
    Raw->SetStringField(TEXT("asset_path"), Asset ? Asset->GetPathName() : FString());
    Raw->SetStringField(TEXT("class"), Asset ? Asset->GetClass()->GetPathName() : FString());
    Raw->SetStringField(TEXT("class_short"), Asset ? ShortClassName(Asset->GetClass()) : FString());
    Raw->SetStringField(TEXT("kind"), Kind);
    Raw->SetStringField(TEXT("schema_key"), SchemaKey());
    const FString PackageName = Asset ? Asset->GetOutermost()->GetName() : FString();
    Raw->SetStringField(TEXT("saved_hash"), PackageSavedHash(PackageName));
    Raw->SetBoolField(TEXT("dirty"), IsPackageDirty(PackageName));
    return Raw;
}
}
