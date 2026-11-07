#include "NexusMovePreflight.h"

#include "Serialization/ArchiveUObject.h"
#include "Misc/RedirectCollector.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge::AssetMove
{
namespace
{
class FCdoReferences : public FArchiveUObject
{
public:
    const TMap<FSoftObjectPath, FSoftObjectPath>& Redirects;
    TArray<TSharedPtr<FJsonValue>>& Results;
    FString Cdo;
    TSet<FString> Seen;

    FCdoReferences(const TMap<FSoftObjectPath, FSoftObjectPath>& InRedirects,
        TArray<TSharedPtr<FJsonValue>>& InResults, UObject* Object)
        : Redirects(InRedirects), Results(InResults), Cdo(Object->GetPathName())
    {
        ArIsObjectReferenceCollector = true;
        ArIsModifyingWeakAndStrongReferences = true;
        SetIsSaving(true);
        ArIgnoreOuterRef = true;
        ArIgnoreArchetypeRef = true;
    }

    using FArchiveUObject::operator<<;

    void Observe(const FSoftObjectPath& Path, const TCHAR* Kind)
    {
        const FSoftObjectPath Asset = Path.GetWithoutSubPath();
        const FSoftObjectPath Redirected = GRedirectCollector.GetAssetPathRedirection(Asset);
        const FSoftObjectPath Match = Redirects.Contains(Asset) ? Asset : Redirected;
        if (!Redirects.Contains(Match) || Seen.Contains(Match.ToString()))
        {
            return;
        }
        Seen.Add(Match.ToString());
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("cdo"), Cdo);
        Row->SetStringField(TEXT("asset"), Match.ToString());
        Row->SetStringField(TEXT("reference_kind"), Kind);
        Results.Add(MakeShared<FJsonValueObject>(Row));
    }

    virtual FArchive& operator<<(UObject*& Object) override
    {
        if (Object)
        {
            Observe(FSoftObjectPath(Object), TEXT("hard"));
        }
        return *this;
    }

    virtual FArchive& operator<<(FSoftObjectPath& Path) override
    {
        Observe(Path, TEXT("soft"));
        return *this;
    }

    virtual FArchive& operator<<(FSoftObjectPtr& Pointer) override
    {
        // Reference-collector archives intentionally skip soft pointers in the
        // default ArchiveUObject serializer; inspect their paths explicitly.
        Observe(Pointer.ToSoftObjectPath(), TEXT("soft"));
        return *this;
    }
};
}

TArray<TSharedPtr<FJsonValue>> CdoReferences(const TMap<FSoftObjectPath, FSoftObjectPath>& Redirects)
{
    TArray<TSharedPtr<FJsonValue>> Results;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        UObject* Cdo = Class->ClassDefaultObject;
        if (!IsValid(Cdo) || Class->ClassGeneratedBy || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        FCdoReferences Archive(Redirects, Results, Cdo);
        Cdo->Serialize(Archive);
    }
    return Results;
}
}
