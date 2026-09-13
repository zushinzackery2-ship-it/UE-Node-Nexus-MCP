#include "NexusSchema.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UeNodeNexusBridgeBuildInfo.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusBridgeRequestDispatch.h"
#include "UeNodeNexusCollaboration.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
struct FFileIdentity
{
    FDateTime Time;
    int64 Size = -1;
    FString Hash;
};

FString Fingerprint(const FString& File)
{
    static TMap<FString, FFileIdentity> Cache;
    const FDateTime Time = IFileManager::Get().GetTimeStamp(*File);
    const int64 Size = IFileManager::Get().FileSize(*File);
    FFileIdentity& Entry = Cache.FindOrAdd(File);
    if (Entry.Time != Time || Entry.Size != Size || Entry.Hash.IsEmpty())
    {
        Entry.Time = Time;
        Entry.Size = Size;
        Entry.Hash = Size >= 0 ? LexToString(FMD5Hash::HashFile(*File)) : TEXT("absent");
    }
    return Entry.Hash;
}

FString TypeGeneration()
{
    static FString Generation = TEXT("base");
    static const FDelegateHandle Handle = FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda([](EReloadCompleteReason)
    {
        Generation = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    });
    return Generation;
}
}

TSharedPtr<FJsonObject> SchemaEnvironment()
{
    const auto Result = MakeShared<FJsonObject>();
    FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
    FPaths::NormalizeFilename(ProjectFile);
    FString ProjectId;
    GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectID"), ProjectId, GGameIni);
    Result->SetStringField(TEXT("project_id"), ProjectId.IsEmpty() ? FMD5::HashAnsiString(*ProjectFile.ToLower()) : ProjectId);
    Result->SetStringField(TEXT("project_file"), ProjectFile);
    Result->SetStringField(TEXT("project_definition"), Fingerprint(ProjectFile));
    Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Result->SetStringField(TEXT("engine_build"), Fingerprint(FPaths::EngineDir() / TEXT("Build/Build.version")));
    Result->SetStringField(TEXT("engine_modules"), Fingerprint(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealEditor.modules")));
    Result->SetObjectField(TEXT("bridge_builds"), BridgeBuildIdentities());
    Result->SetNumberField(TEXT("bridge_contract"), NEXUS_CONTRACT_VERSION);
    Result->SetStringField(TEXT("native_type_generation"), TypeGeneration());
    TArray<FString> Plugins;
    TSet<FName> ModuleNames;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        Plugins.Add(Plugin->GetName() + TEXT(":") + Fingerprint(Plugin->GetDescriptorFileName()));
        for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
        {
            ModuleNames.Add(Module.Name);
        }
    }
    Plugins.Sort();
    TArray<TSharedPtr<FJsonValue>> PluginRows;
    for (const FString& Plugin : Plugins)
    {
        PluginRows.Add(MakeShared<FJsonValueString>(Plugin));
    }
    Result->SetArrayField(TEXT("plugins"), PluginRows);
    if (const FProjectDescriptor* Project = IProjectManager::Get().GetCurrentProject())
    {
        for (const FModuleDescriptor& Module : Project->Modules)
        {
            ModuleNames.Add(Module.Name);
        }
    }
    TArray<FName> Modules = ModuleNames.Array();
    Modules.Sort(FNameLexicalLess());
    TArray<TSharedPtr<FJsonValue>> ModuleRows;
    for (const FName Module : Modules)
    {
        FString File;
        FModuleStatus Status;
        if (FModuleManager::Get().QueryModule(Module, Status))
        {
            File = Status.FilePath;
        }
        else
        {
            FModuleManager::Get().ModuleExists(*Module.ToString(), &File);
        }
        if (!File.IsEmpty())
        {
            File = FPaths::ConvertRelativePathToFull(File);
            FPaths::NormalizeFilename(File);
        }
        const FString Stamp = Module.ToString() + TEXT(":") + File + TEXT(":")
            + IFileManager::Get().GetTimeStamp(*File).ToIso8601() + TEXT(":")
            + LexToString(IFileManager::Get().FileSize(*File));
        ModuleRows.Add(MakeShared<FJsonValueString>(Stamp));
    }
    Result->SetArrayField(TEXT("modules"), ModuleRows);
    const auto Config = MakeShared<FJsonObject>();
    for (const TCHAR* Name : { TEXT("DefaultEngine.ini"), TEXT("DefaultGame.ini"), TEXT("DefaultEditor.ini"), TEXT("DefaultInput.ini") })
    {
        Config->SetStringField(Name, Fingerprint(FPaths::ProjectConfigDir() / Name));
    }
    Result->SetObjectField(TEXT("configuration"), Config);
    return Result;
}

FString SchemaKey()
{
    static FString RequestId, Key;
    const FString Current = ActiveBridgeRequestId();
    if (Current.IsEmpty() || Current != RequestId || Key.IsEmpty())
    {
        Key = FString::Printf(TEXT("%s-%s"), *FEngineVersion::Current().ToString(EVersionComponent::Patch),
            *Collaboration::ContentDigest(SchemaEnvironment()).Left(24));
        RequestId = Current;
    }
    return Key;
}
}
