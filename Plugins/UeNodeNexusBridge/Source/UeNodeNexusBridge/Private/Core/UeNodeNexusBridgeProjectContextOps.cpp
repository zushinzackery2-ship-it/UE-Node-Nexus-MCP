#include "UeNodeNexusBridgeOperations.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static FString NormalizePathForCompare(FString Path)
{
    if (Path.IsEmpty())
    {
        return FString();
    }

    FPaths::NormalizeDirectoryName(Path);
    FPaths::MakeStandardFilename(Path);
    if (!Path.EndsWith(TEXT("/")))
    {
        Path += TEXT("/");
    }
    return Path;
}

static FString AbsolutePath(FString Path)
{
    if (Path.IsEmpty())
    {
        return FString();
    }

    Path = FPaths::ConvertRelativePathToFull(Path);
    FPaths::MakeStandardFilename(Path);
    return Path;
}

static bool DirectoryExists(const FString& Path)
{
    return !Path.IsEmpty() && IFileManager::Get().DirectoryExists(*Path);
}

static bool FileExists(const FString& Path)
{
    return !Path.IsEmpty() && IFileManager::Get().FileExists(*Path);
}

TSharedPtr<FJsonObject> HandleProjectContextGet(const FString& Operation, const FString& RequestId)
{
    const FString ProjectFilePath = AbsolutePath(FPaths::GetProjectFilePath());
    const FString ProjectDir = AbsolutePath(FPaths::ProjectDir());
    const FString ProjectContentDir = AbsolutePath(FPaths::ProjectContentDir());
    const FString ProjectSavedDir = AbsolutePath(FPaths::ProjectSavedDir());
    const FString EngineDir = AbsolutePath(FPaths::EngineDir());
    const FString LaunchDir = AbsolutePath(FPaths::LaunchDir());
    const FString BaseDir = AbsolutePath(FPlatformProcess::BaseDir());
    const FString UserDir = AbsolutePath(FPlatformProcess::UserDir());
    const FString ExecutablePath = AbsolutePath(FPlatformProcess::ExecutablePath());
    const FString GamePackageRootFilename = AbsolutePath(FPackageName::LongPackageNameToFilename(TEXT("/Game")));

    TSharedPtr<FJsonObject> Mounts = MakeShared<FJsonObject>();
    Mounts->SetStringField(TEXT("game_long_package_root"), TEXT("/Game"));
    Mounts->SetStringField(TEXT("game_long_package_to_filename"), GamePackageRootFilename);
    Mounts->SetBoolField(
        TEXT("game_mount_matches_project_content"),
        NormalizePathForCompare(GamePackageRootFilename).Equals(NormalizePathForCompare(ProjectContentDir), ESearchCase::IgnoreCase)
    );

    TSharedPtr<FJsonObject> Checks = MakeShared<FJsonObject>();
    Checks->SetBoolField(TEXT("project_file_exists"), FileExists(ProjectFilePath));
    Checks->SetBoolField(TEXT("project_dir_exists"), DirectoryExists(ProjectDir));
    Checks->SetBoolField(TEXT("project_content_dir_exists"), DirectoryExists(ProjectContentDir));
    Checks->SetBoolField(TEXT("project_saved_dir_exists"), DirectoryExists(ProjectSavedDir));
    Checks->SetBoolField(TEXT("engine_dir_exists"), DirectoryExists(EngineDir));
    Checks->SetBoolField(TEXT("game_content_dir_exists"), DirectoryExists(GamePackageRootFilename));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
    Data->SetStringField(TEXT("project_file_path"), ProjectFilePath);
    Data->SetStringField(TEXT("project_dir"), ProjectDir);
    Data->SetStringField(TEXT("project_content_dir"), ProjectContentDir);
    Data->SetStringField(TEXT("project_saved_dir"), ProjectSavedDir);
    Data->SetStringField(TEXT("engine_dir"), EngineDir);
    Data->SetStringField(TEXT("launch_dir"), LaunchDir);
    Data->SetStringField(TEXT("base_dir"), BaseDir);
    Data->SetStringField(TEXT("user_dir"), UserDir);
    Data->SetStringField(TEXT("platform_user_dir"), UserDir);
    Data->SetStringField(TEXT("executable_path"), ExecutablePath);
    Data->SetStringField(TEXT("command_line"), FCommandLine::Get());
    Data->SetObjectField(TEXT("mounts"), Mounts);
    Data->SetObjectField(TEXT("checks"), Checks);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
