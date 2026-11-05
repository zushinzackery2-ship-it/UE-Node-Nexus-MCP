#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace NexusTests
{
// Assets written under a unique /Game folder of the host project. Redirector and
// deletion behaviour only exists for packages that are really on disk.
struct FDiskAssets
{
    FString Root;

    explicit FDiskAssets(const TCHAR* Label)
        : Root(FString(TEXT("/Game/")) + Label + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits))
    {
    }

    FString Package(const TCHAR* Relative) const
    {
        return Root / Relative;
    }

    UBlueprint* Blueprint(const TCHAR* Relative, UClass* Parent) const
    {
        const FString PackageName = Package(Relative);
        UPackage* Outer = CreatePackage(*PackageName);
        UBlueprint* Result = FKismetEditorUtilities::CreateBlueprint(Parent, Outer, FName(*FPackageName::GetShortName(PackageName)),
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        FAssetRegistryModule::AssetCreated(Result);
        FKismetEditorUtilities::CompileBlueprint(Result);
        return Result;
    }

    static bool Save(UPackage* Outer, FString& Error)
    {
        return UeNodeNexusBridge::Transcode::SavePackageDirect(Outer, nullptr, Error);
    }

    static bool Move(UObject* Asset, const FString& Destination, FString& Error)
    {
        const FString Source = Asset->GetOutermost()->GetName();
        ObjectTools::FPackageGroupName Target;
        Target.PackageName = Destination;
        Target.ObjectName = FPackageName::GetShortName(Destination);
        TSet<UPackage*> Refused;
        FText RenameError;
        // AssetTools can fix all loaded users and remove the source. These
        // fixtures need the native package/class/CDO redirectors left intact.
        if (!ObjectTools::RenameSingleObject(Asset, Target, Refused, RenameError, nullptr, true))
        {
            Error = TEXT("fixture move failed: ") + RenameError.ToString();
            return false;
        }
        UPackage* Redirectors = FindPackage(nullptr, *Source);
        if (Redirectors == nullptr)
        {
            Error = TEXT("fixture move did not retain the source redirector package: ") + Source;
            return false;
        }
        return Save(Asset->GetOutermost(), Error) && Save(Redirectors, Error);
    }

    static FString File(const FString& PackageName)
    {
        FString Filename;
        return FPackageName::DoesPackageExist(PackageName, &Filename) ? FPaths::ConvertRelativePathToFull(Filename) : FString();
    }

    // Discards in-memory state, so the next load reads what is on disk.
    static void Unload(const TArray<FString>& PackageNames)
    {
        TArray<UPackage*> Packages;
        for (const FString& Name : PackageNames)
        {
            if (UPackage* Found = FindPackage(nullptr, *Name))
            {
                Found->SetDirtyFlag(false);
                Packages.Add(Found);
            }
        }
        if (!Packages.IsEmpty())
        {
            UPackageTools::FUnloadPackageParams Params(Packages);
            Params.bResetTransBuffer = false;
            UPackageTools::UnloadPackages(Params);
        }
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    }

    void Rescan() const
    {
        FAssetRegistryModule::GetRegistry().ScanPathsSynchronous({ Root }, true);
    }

    void Cleanup() const
    {
        TArray<FString> Names;
        for (TObjectIterator<UPackage> It; It; ++It)
        {
            if (It->GetName().StartsWith(Root + TEXT("/")))
            {
                Names.Add(It->GetName());
            }
        }
        Unload(Names);
        IFileManager::Get().DeleteDirectory(*FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Root)), false, true);
        Rescan();
    }
};
}
