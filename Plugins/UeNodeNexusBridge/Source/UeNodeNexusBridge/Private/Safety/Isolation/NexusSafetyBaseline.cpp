#include "NexusSafetyBaseline.h"
#include "NexusSafetyEnvironment.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Safety
{
TSharedPtr<FJsonObject> ExportBaseline(const FString& Operation, const FString& RequestId,
    const TSharedPtr<FJsonObject>& Payload)
{
    const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
    if (!Payload->TryGetArrayField(TEXT("asset_paths"), Paths) || Paths == nullptr
        || Paths->Num() == 0 || Paths->Num() > 512)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_paths requires 1..512 guarded assets"));
    }
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    TSet<FName> Seen;
    TArray<FName> Packages;
    for (const auto& Value : *Paths)
    {
        FString Path;
        if (!Value.IsValid() || !Value->TryGetString(Path) || !Path.StartsWith(TEXT("/Game/")))
        {
            return MakeOperationError(Operation, RequestId, TEXT("isolation_target_rejected"), TEXT("baseline assets must belong to /Game"));
        }
        const FString Package = FPackageName::ObjectPathToPackageName(Path);
        if (!FPackageName::IsValidLongPackageName(Package))
        {
            return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("invalid package in asset_paths"));
        }
        Seen.Add(FName(*Package));
    }
    Packages = Seen.Array();
    auto& Registry = FAssetRegistryModule::GetRegistry();
    for (int32 Index = 0; Index < Packages.Num(); ++Index)
    {
        TArray<FName> Dependencies;
        Registry.GetDependencies(Packages[Index], Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        for (const FName Dependency : Dependencies)
        {
            if (Dependency.ToString().StartsWith(TEXT("/Game/")) && !Seen.Contains(Dependency))
            {
                Seen.Add(Dependency);
                Packages.Add(Dependency);
            }
        }
        if (Packages.Num() > 4096)
        {
            return MakeOperationError(Operation, RequestId, TEXT("isolation_baseline_budget"), TEXT("baseline dependency closure exceeds 4096 packages"));
        }
    }
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
        / TEXT("Nexus/SafetyBaseline") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    TArray<TSharedPtr<FJsonValue>> Files;
    FString Error;
    for (const FName Name : Packages)
    {
        UPackage* Package = FindPackage(nullptr, *Name.ToString());
        if (Package == nullptr || bDryRun)
        {
            continue;
        }
        const FString Original = Collaboration::PackageFile(Package);
        FString Relative = Original;
        const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
        if (!FPaths::MakePathRelativeTo(Relative, *Project) || !Relative.StartsWith(TEXT("Content/"))
            || Relative.Contains(TEXT("../")))
        {
            return MakeOperationError(Operation, RequestId, TEXT("isolation_path_rejected"), TEXT("package file must remain under project Content"));
        }
        const FString Copy = Root / Relative;
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Copy), true);
        const bool bDirty = Package->IsDirty();
        const FString Before = Collaboration::FileHash(Original);
        const bool bSaved = Transcode::SavePackageTo(Package, nullptr, Copy, true, Error);
        Package->SetDirtyFlag(bDirty);
        if (!bSaved || Before != Collaboration::FileHash(Original))
        {
            return MakeOperationError(Operation, RequestId, TEXT("isolation_baseline_failed"), Error.IsEmpty()
                ? TEXT("original package changed during baseline serialization") : Error);
        }
        const TArray<FString> Extensions
        {
            FPaths::GetExtension(Original, true), TEXT(".uexp"), TEXT(".ubulk"), TEXT(".uptnl"), TEXT(".m.ubulk")
        };
        for (const FString& Extension : Extensions)
        {
            auto Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("relative"), FPaths::ChangeExtension(Relative, Extension));
            Row->SetStringField(TEXT("source"), FPaths::ChangeExtension(Copy, Extension));
            Row->SetStringField(TEXT("hash"), Collaboration::FileHash(FPaths::ChangeExtension(Copy, Extension)));
            Files.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("root"), Root);
    Data->SetObjectField(TEXT("environment"), Environment());
    Data->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
    Data->SetArrayField(TEXT("files"), Files);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetNumberField(TEXT("package_count"), Packages.Num());
    UE_LOG(LogTemp, Display, TEXT("Nexus safety baseline request=%s packages=%d files=%d dry_run=%d"),
        *RequestId, Packages.Num(), Files.Num(), bDryRun);
    auto Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
