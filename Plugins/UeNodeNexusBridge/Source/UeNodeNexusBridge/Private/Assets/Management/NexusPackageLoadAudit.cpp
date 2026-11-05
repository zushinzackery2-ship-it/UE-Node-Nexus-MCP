#include "NexusPackageLoadAudit.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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
    return Row;
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
        const FLinkerLoad* Linker = Audit.Package ? FLinkerLoad::FindExistingLinkerForPackage(Audit.Package) : nullptr;
        if (Linker == nullptr)
        {
            Audit.Error = Audit.Package ? TEXT("export_table_unavailable") : TEXT("package_load_failed");
            continue;
        }
        Audit.bAudited = true;
        Audit.Exports = Linker->ExportMap.Num();
        for (const FObjectExport& Export : Linker->ExportMap)
        {
            if (Export.bExportLoadFailed || (Export.Object == nullptr && !Export.bWasFiltered))
            {
                Audit.FailedExports.Add(Export.ObjectName.ToString());
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
