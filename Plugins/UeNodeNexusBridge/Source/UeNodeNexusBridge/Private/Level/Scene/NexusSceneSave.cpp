#include "NexusSceneApply.h"

#include "GameFramework/Actor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Scene
{
void FApply::Touch(AActor* Actor, bool bDeleted)
{
    UPackage* Package = Actor->GetPackage();
    const bool bExternal = Actor->IsPackageExternal();
    FSaveTarget Target;
    Target.Package = Package;
    Target.Base = bExternal ? nullptr : static_cast<UObject*>(Actor->GetLevel()->GetTypedOuter<UWorld>());
    Target.bDeleted = bDeleted && bExternal;
    Packages.Add(Package->GetName(), Target);
    UPackage* LevelPackage = Actor->GetLevel()->GetOutermost();
    if (bExternal)
    {
        FSaveTarget LevelTarget;
        LevelTarget.Package = LevelPackage;
        LevelTarget.Base = Actor->GetLevel()->GetTypedOuter<UWorld>();
        Packages.Add(LevelPackage->GetName(), LevelTarget);
    }
}

bool SaveScene(FApply& Context, FRows& Saved, FRows& Failed)
{
    TArray<FString> Names;
    Context.Packages.GetKeys(Names);
    Names.Sort();
    for (const FString& Name : Names)
    {
        FSaveTarget& Target = Context.Packages[Name];
        if (!Target.bDeleted && !Target.Package->IsDirty())
        {
            continue;
        }
        FObject Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("package"), Name);
        Row->SetBoolField(TEXT("deleted"), Target.bDeleted);
        FString Error;
        bool bOk = false;
        if (Target.bDeleted)
        {
            const FString File = FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension());
            if (Transcode::IsPackageFileReadOnly(Target.Package))
            {
                Error = TEXT("save_blocked_read_only: ") + File;
            }
            else
            {
                bOk = !IFileManager::Get().FileExists(*File) || IFileManager::Get().Delete(*File, false, false, true);
                if (bOk)
                {
                    Target.Package->SetDirtyFlag(false);
                }
                else
                {
                    Error = TEXT("external_package_delete_failed: ") + File;
                }
            }
        }
        else
        {
            bOk = Transcode::SavePackageDirect(Target.Package, Target.Base, Error);
        }
        Row->SetBoolField(TEXT("saved"), bOk);
        Row->SetStringField(TEXT("error"), Error);
        UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=scene_save package=%s deleted=%d ok=%d error=%s"),
            *ActiveBridgeRequestId(), *Name, Target.bDeleted, bOk, *Error);
        (bOk ? Saved : Failed).Add(MakeShared<FJsonValueObject>(Row));
    }
    return Failed.IsEmpty();
}
}
