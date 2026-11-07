#include "NexusPackageLoadAudit.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/DynamicBlueprintBinding.h"
#include "Misc/PackageName.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> FPackageLoadAudit::ToJson() const
{
    auto Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), PackageName);
    Row->SetBoolField(TEXT("loaded_here"), bLoadedHere);
    Row->SetBoolField(TEXT("audited"), bAudited);
    Row->SetNumberField(TEXT("exports"), Exports);
    Row->SetBoolField(TEXT("complete"), IsComplete());
    Row->SetStringField(TEXT("error"), Error);
    TArray<TSharedPtr<FJsonValue>> Failed;
    for (const FString& Name : FailedExports)
    {
        Failed.Add(MakeShared<FJsonValueString>(Name));
    }
    Row->SetArrayField(TEXT("failed_exports"), Failed);
    TArray<TSharedPtr<FJsonValue>> FailedPaths;
    for (const FString& Path : FailedExportPaths)
    {
        FailedPaths.Add(MakeShared<FJsonValueString>(Path));
    }
    Row->SetArrayField(TEXT("failed_export_paths"), FailedPaths);
    TArray<TSharedPtr<FJsonValue>> Regenerated;
    for (const FString& Name : RegeneratedExports)
    {
        Regenerated.Add(MakeShared<FJsonValueString>(Name));
    }
    Row->SetArrayField(TEXT("regenerated_exports"), Regenerated);
    return Row;
}

static bool HasRegeneratedReplacement(FLinkerLoad& Linker, int32 Index, UPackage* Package)
{
    const FString Path = Linker.GetExportPathName(Index);
    UObject* Replacement = StaticFindObject(UObject::StaticClass(), nullptr, *Path);
    if (!IsValid(Replacement) || Replacement->GetOutermost() != Package
        || (!Replacement->IsA<UFunction>() && !Replacement->IsA<UDynamicBlueprintBinding>()))
    {
        return false;
    }
    auto* Generated = Replacement->GetTypedOuter<UBlueprintGeneratedClass>();
    const auto* Blueprint = Generated ? Cast<UBlueprint>(Generated->ClassGeneratedBy) : nullptr;
    if (!Blueprint || Blueprint->GeneratedClass != Generated || Blueprint->Status == BS_Error)
    {
        return false;
    }
    const FPackageIndex Class = Linker.ExportMap[Index].ClassIndex;
    const FString ClassPath = Class.IsImport() ? Linker.GetImportPathName(Class.ToImport())
        : Class.IsExport() ? Linker.GetExportPathName(Class.ToExport()) : FString();
    return Replacement->GetClass()->GetPathName() == ClassPath;
}

TArray<FPackageLoadAudit> LoadPackagesAudited(const TArray<FString>& PackageNames)
{
    TArray<FPackageLoadAudit> Audits;
    TArray<TStrongObjectPtr<UPackage>> KeepPackages;
    for (const FString& Name : PackageNames)
    {
        FPackageLoadAudit& Audit = Audits.AddDefaulted_GetRef();
        Audit.PackageName = Name;
        Audit.Package = FindPackage(nullptr, *Name);
        Audit.bLoadedHere = Audit.Package == nullptr
            || (!Audit.Package->IsFullyLoaded() && FPackageName::DoesPackageExist(Name));
        KeepPackages.Emplace(Audit.Package);
    }
    for (FPackageLoadAudit& Audit : Audits)
    {
        if (!Audit.bLoadedHere)
        {
            continue;
        }
        Audit.Package = LoadPackage(nullptr, *Audit.PackageName, LOAD_None);
        KeepPackages.Emplace(Audit.Package);
        FLinkerLoad* Linker = Audit.Package ? FLinkerLoad::FindExistingLinkerForPackage(Audit.Package) : nullptr;
        if (Linker == nullptr)
        {
            Audit.Error = Audit.Package ? TEXT("export_table_unavailable") : TEXT("package_load_failed");
            continue;
        }
        Audit.bAudited = true;
        Audit.Exports = Linker->ExportMap.Num();
        for (int32 Index = 0; Index < Linker->ExportMap.Num(); ++Index)
        {
            const FObjectExport& Export = Linker->ExportMap[Index];
            if (Export.bExportLoadFailed || (Export.Object == nullptr && !Export.bWasFiltered))
            {
                if (HasRegeneratedReplacement(*Linker, Index, Audit.Package))
                {
                    Audit.RegeneratedExports.Add(Linker->GetExportPathName(Index));
                    continue;
                }
                Audit.FailedExports.Add(Export.ObjectName.ToString());
                Audit.FailedExportPaths.Add(Linker->GetExportPathName(Index));
                Audit.Error = TEXT("export_load_incomplete");
            }
        }
    }
    for (const FPackageLoadAudit& Audit : Audits)
    {
        UE_LOG(LogTemp, Display, TEXT("Nexus phase=package_load_audit package=%s loaded_here=%d audited=%d exports=%d failed_exports=%d complete=%d error=%s"),
            *Audit.PackageName, Audit.bLoadedHere, Audit.bAudited, Audit.Exports, Audit.FailedExports.Num(), Audit.IsComplete(), *Audit.Error);
    }
    return Audits;
}
}
