#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;

// Shared text-mirror (transcode) helpers exported for sibling plugins (Niagara bridge).
namespace UeNodeNexusBridge::Transcode
{
struct FApplyFailure
{
    int32 Index = INDEX_NONE;
    FString Code;
    FString Message;
};

// Mutable per-apply state shared by the verb appliers.
struct FApplyContext
{
    // local id -> GUID string (existing nodes from the base, plus nodes created in this plan)
    TMap<FString, FString> Ids;
    // local id -> GUID string for nodes created by this plan (returned to Python as id_map)
    TMap<FString, FString> Created;
    TArray<FApplyFailure> Failures;
    bool bChanged = false;
    bool bDryRun = false;

    void Fail(int32 Index, const FString& Code, const FString& Message)
    {
        Failures.Add({ Index, Code, Message });
    }
};

// --- mirror root -----------------------------------------------------------
UENODENEXUSBRIDGE_API bool SetMirrorRoot(const FString& Root, FString& OutError);
UENODENEXUSBRIDGE_API FString GetMirrorRoot();
UENODENEXUSBRIDGE_API bool IsInsideMirrorRoot(const FString& Path);
// out_dir/<relative package path>.json, validated against the root.
UENODENEXUSBRIDGE_API bool ResolveRawFile(const FString& OutDir, const FString& AssetPath, FString& OutFile, FString& OutError);
UENODENEXUSBRIDGE_API bool WriteJsonFile(const FString& File, const TSharedPtr<FJsonObject>& Json, FString& OutError);

// --- classification / envelope --------------------------------------------
UENODENEXUSBRIDGE_API FString KindForClass(const UClass* Class);
UENODENEXUSBRIDGE_API bool IsGenericAssetClass(const UClass* Class);
UENODENEXUSBRIDGE_API FString ShortClassName(const UClass* Class);
UENODENEXUSBRIDGE_API FString SchemaKey();
UENODENEXUSBRIDGE_API FString EngineVersionString();
UENODENEXUSBRIDGE_API FString PackageSavedHash(const FString& PackageName);
UENODENEXUSBRIDGE_API bool IsPackageDirty(const FString& PackageName);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeRawEnvelope(UObject* Asset, const FString& Kind);

// --- reflection props ------------------------------------------------------
UENODENEXUSBRIDGE_API bool IsEditableProperty(const FProperty* Property);
UENODENEXUSBRIDGE_API FString ExportPropertyValue(const UObject* Object, FProperty* Property);
// [{name,type,value,default}] for every editable property; Defaults may be nullptr (class CDO is used).
UENODENEXUSBRIDGE_API TArray<TSharedPtr<FJsonValue>> ExportEditableProps(UObject* Object, UObject* Defaults = nullptr);
UENODENEXUSBRIDGE_API bool ImportPropertyValue(UObject* Object, const FString& Name, const FString& Value, FString& OutError);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> PropertySchemaJson(FProperty* Property, UObject* Cdo);

// --- save ------------------------------------------------------------------
// UPackage::SavePackage without prompts / SaveAll. Every bridge save goes through here:
// FEditorFileUtils-style saves open a modal "cannot check out from source control"
// dialog on the game thread, which re-enters the message pump while shader jobs are
// in flight and has crashed the editor. A read-only file (checked-in under SCC) is
// reported as OutCode == "save_blocked_read_only" instead of being attempted.
// If another engine operation owns an asset-streaming suspension, the save is
// rejected as "save_blocked_asset_streaming_suspended"; callers retry later.
UENODENEXUSBRIDGE_API bool SavePackageDirect(UObject* Asset, FString& OutError);
UENODENEXUSBRIDGE_API bool SavePackageDirect(UPackage* Package, UObject* Base, FString& OutError, FString* OutCode = nullptr);
// True when the package's .uasset/.umap exists on disk and is read-only.
UENODENEXUSBRIDGE_API bool IsPackageFileReadOnly(const UPackage* Package);
// Error code for a failed SavePackageDirect ("save_blocked_read_only" |
// "save_blocked_asset_streaming_suspended" | "save_failed").
UENODENEXUSBRIDGE_API FString SaveErrorCode(const FString& Error);
}
