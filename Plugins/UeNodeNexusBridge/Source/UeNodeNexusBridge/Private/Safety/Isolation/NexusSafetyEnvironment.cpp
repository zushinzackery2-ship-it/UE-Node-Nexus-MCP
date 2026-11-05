#include "NexusSafetyEnvironment.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "Dom/JsonObject.h"
#include "NexusValidation.h"
#include "UeNodeNexusBridgeTranscode.h"

namespace UeNodeNexusBridge::Safety
{
TSharedPtr<FJsonObject> Environment()
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
    Result->SetStringField(TEXT("engine_build"), Collaboration::FileHash(FPaths::EngineDir() / TEXT("Build/Build.version")));
    Result->SetStringField(TEXT("engine_modules"), Collaboration::FileHash(FPaths::EngineDir() / TEXT("Binaries/Win64/UnrealEditor.modules")));
    Result->SetStringField(TEXT("project_definition"), Collaboration::FileHash(FPaths::GetProjectFilePath()));
    auto Configuration = MakeShared<FJsonObject>();
    for (const TCHAR* Name : {TEXT("DefaultEngine.ini"), TEXT("DefaultGame.ini"), TEXT("DefaultEditor.ini"), TEXT("DefaultInput.ini")})
    {
        Configuration->SetStringField(Name, Collaboration::FileHash(FPaths::ProjectConfigDir() / Name));
    }
    Result->SetObjectField(TEXT("configuration"), Configuration);
    Result->Values.Append(NexusLifecycle::Validation::Environment()->Values);
    auto Plugins = MakeShared<FJsonObject>();
    TArray<FString> Mounts;
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        Plugins->SetStringField(Plugin->GetName(), Collaboration::FileHash(Plugin->GetDescriptorFileName()));
        if (Plugin->CanContainContent())
        {
            Mounts.Add(Plugin->GetName());
        }
    }
    Result->SetObjectField(TEXT("plugins"), Plugins);
    Mounts.Sort();
    TArray<TSharedPtr<FJsonValue>> Values;
    for (const FString& Mount : Mounts)
    {
        Values.Add(MakeShared<FJsonValueString>(Mount));
    }
    Result->SetArrayField(TEXT("content_mounts"), Values);
    auto ProjectMounts = MakeShared<FJsonObject>();
    const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    for (FString Root : Transcode::ProjectContentRoots())
    {
        FString Directory;
        if (!FPackageName::TryConvertLongPackageNameToFilename(Root, Directory))
        {
            continue;
        }
        Directory = FPaths::ConvertRelativePathToFull(Directory);
        if (!FPaths::MakePathRelativeTo(Directory, *Project))
        {
            continue;
        }
        FPaths::NormalizeDirectoryName(Directory);
        Root.RemoveFromStart(TEXT("/"));
        Root.RemoveFromEnd(TEXT("/"));
        // Relative targets bind the source and copied project's real registrations.
        ProjectMounts->SetStringField(Root, Directory);
    }
    Result->SetObjectField(TEXT("project_content_mounts"), ProjectMounts);
    return Result;
}
}
