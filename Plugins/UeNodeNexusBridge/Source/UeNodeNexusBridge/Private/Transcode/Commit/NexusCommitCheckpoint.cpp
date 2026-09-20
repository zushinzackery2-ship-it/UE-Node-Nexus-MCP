#include "NexusPackageFiles.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Collaboration
{
bool Checkpoint(const FJson& Request, const FJson& Before, const FJson& Receipt, FString& Error)
{
    TSet<UPackage*> Packages;
    if (Text(Request, TEXT("kind")) == TEXT("scene"))
    {
        if (UPackage* Map = FindPackage(nullptr, *Text(Before, TEXT("map_path"))))
        {
            Packages.Add(Map);
        }
        for (const auto& Value : Rows(Before, TEXT("actors")))
        {
            AActor* Actor = FindObject<AActor>(nullptr, *Text(Value->AsObject(), TEXT("actor_path")));
            if (Actor)
            {
                Packages.Add(Actor->GetPackage());
                Packages.Add(Actor->GetLevel()->GetOutermost());
            }
        }
        for (const auto& Value : Rows(Object(Request, TEXT("scene_plan")), TEXT("ops")))
        {
            const FString Level = Text(Value->AsObject(), TEXT("level_path"));
            if (UPackage* Package = Level.IsEmpty() ? nullptr : FindPackage(nullptr, *Level))
            {
                Packages.Add(Package);
            }
        }
    }
    else if (UObject* Asset = LoadObject<UObject>(nullptr, *Text(Request, TEXT("asset_path"))))
    {
        Packages.Add(Asset->GetOutermost());
    }
    for (UPackage* Package : Packages)
    {
        if (!CapturePackage(Package, Receipt, Error))
        {
            return false;
        }
    }
    CaptureRecoveryMemory(Receipt, TEXT("package_memory_before"));
    return SaveReceipt(Receipt, TEXT("prepared"), Error);
}

static FJson NewPackage(UPackage* Package)
{
    FJson Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), Package->GetName());
    Row->SetStringField(TEXT("file"), PackageFile(Package));
    Row->SetBoolField(TEXT("existed"), false);
    TArray<TSharedPtr<FJsonValue>> Files;
    for (const FString& Extension : { FPaths::GetExtension(PackageFile(Package), true), FString(TEXT(".uexp")), FString(TEXT(".ubulk")) })
    {
        FJson Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), FPaths::ChangeExtension(PackageFile(Package), Extension));
        Item->SetStringField(TEXT("disk_hash"), TEXT("absent"));
        Item->SetStringField(TEXT("memory_hash"), TEXT("absent"));
        Files.Add(MakeShared<FJsonValueObject>(Item));
    }
    Row->SetArrayField(TEXT("files"), Files);
    return Row;
}

bool SavePackages(const FJson& Request, const FJson& Receipt, FString& Error)
{
    auto Packages = Rows(Receipt, TEXT("packages"));
    if (Flag(Request, TEXT("delete_asset")))
    {
        for (const auto& Value : Packages)
        {
            const FJson Row = Value->AsObject();
            Row->SetBoolField(TEXT("deleted"), true);
            Row->SetStringField(TEXT("save_phase"), TEXT("saved"));
            for (const auto& FileValue : Rows(Row, TEXT("files")))
            {
                const FJson File = FileValue->AsObject();
                File->SetStringField(TEXT("saved_hash"), FileHash(Text(File, TEXT("path"))));
            }
        }
        return SaveReceipt(Receipt, TEXT("saving"), Error);
    }
    TMap<FString, FJson> ByName;
    for (const auto& Value : Packages)
    {
        ByName.Add(Text(Value->AsObject(), TEXT("package")), Value->AsObject());
    }
    auto Touched = Rows(Object(Receipt, TEXT("response_data")), TEXT("touched_packages"));
    if (Text(Request, TEXT("kind")) != TEXT("scene"))
    {
        FJson Target = MakeShared<FJsonObject>();
        Target->SetStringField(TEXT("package"), FPackageName::ObjectPathToPackageName(Text(Request, TEXT("asset_path"))));
        Touched.Add(MakeShared<FJsonValueObject>(Target));
    }
    for (const auto& Value : Touched)
    {
        const FJson Target = Value->AsObject();
        const FString Name = Text(Target, TEXT("package"));
        UPackage* Package = FindPackage(nullptr, *Name);
        if (!Package)
        {
            Error = TEXT("touched package is unavailable: ") + Name;
            return false;
        }
        FJson Row = ByName.FindRef(Name);
        if (!Row.IsValid())
        {
            if (IFileManager::Get().FileExists(*PackageFile(Package)))
            {
                Error = TEXT("uncheckpointed existing package: ") + Name;
                return false;
            }
            Row = NewPackage(Package);
            Packages.Add(MakeShared<FJsonValueObject>(Row));
            ByName.Add(Name, Row);
            Receipt->SetArrayField(TEXT("packages"), Packages);
        }
        Row->SetBoolField(TEXT("deleted"), Flag(Target, TEXT("deleted")));
        Row->SetStringField(TEXT("save_phase"), TEXT("pending"));
    }
    if (!SaveReceipt(Receipt, TEXT("saving"), Error))
    {
        return false;
    }
    for (const auto& Value : Touched)
    {
        const FJson Target = Value->AsObject();
        const FJson Row = ByName[Text(Target, TEXT("package"))];
        UPackage* Package = FindPackage(nullptr, *Text(Row, TEXT("package")));
        Row->SetStringField(TEXT("save_phase"), TEXT("writing"));
        if (!SaveReceipt(Receipt, TEXT("saving"), Error))
        {
            return false;
        }
        if (Flag(Row, TEXT("deleted")))
        {
            const FString File = Text(Row, TEXT("file"));
            if (IFileManager::Get().FileExists(*File) && !IFileManager::Get().Delete(*File, false, false, true))
            {
                Error = TEXT("cannot remove external actor package: ") + File;
                return false;
            }
            Package->SetDirtyFlag(false);
        }
        else if (!SaveStagedPackage(Package, Receipt, Row, Error))
        {
            return false;
        }
        for (const auto& FileValue : Rows(Row, TEXT("files")))
        {
            const FJson File = FileValue->AsObject();
            File->SetStringField(TEXT("saved_hash"), FileHash(Text(File, TEXT("path"))));
        }
        Row->SetStringField(TEXT("save_phase"), TEXT("saved"));
        if (!SaveReceipt(Receipt, TEXT("saving"), Error))
        {
            return false;
        }
    }
    return true;
}
}
