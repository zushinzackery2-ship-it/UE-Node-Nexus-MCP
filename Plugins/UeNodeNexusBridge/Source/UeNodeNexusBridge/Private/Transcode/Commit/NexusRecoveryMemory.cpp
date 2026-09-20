#include "NexusPackageFiles.h"

#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/ObjectWriter.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace UeNodeNexusBridge::Collaboration
{
static TSet<FString> RecoveryPackages(const FJson& Receipt)
{
    TSet<FString> Names;
    for (const auto& Value : Rows(Receipt, TEXT("packages")))
    {
        Names.Add(Text(Value->AsObject(), TEXT("package")));
    }
    for (const auto& Value : Rows(Object(Receipt, TEXT("response_data")), TEXT("touched_packages")))
    {
        Names.Add(Text(Value->AsObject(), TEXT("package")));
    }
    const FJson Request = Object(Receipt, TEXT("request"));
    if (Text(Request, TEXT("kind")) != TEXT("scene"))
    {
        Names.Add(FPackageName::ObjectPathToPackageName(Text(Request, TEXT("asset_path"))));
    }
    Names.Remove(FString());
    return Names;
}

static FString MemoryDigest(UPackage* Package)
{
    TArray<UObject*> Objects;
    GetObjectsWithPackage(Package, Objects, true);
    Objects.Sort([](const UObject& A, const UObject& B)
    {
        return A.GetPathName() < B.GetPathName();
    });
    FMD5 Hash;
    for (UObject* Object : Objects)
    {
        if (Object->HasAnyFlags(RF_Transient))
        {
            continue;
        }
        const FTCHARToUTF8 Name(*Object->GetPathName());
        Hash.Update(reinterpret_cast<const uint8*>(Name.Get()), Name.Length() + 1);
        TArray<uint8> Bytes;
        FObjectWriter Writer(Object, Bytes, false, false, false, PPF_DuplicateVerbatim);
        if (Writer.IsError())
        {
            return FString();
        }
        Hash.Update(Bytes.GetData(), Bytes.Num());
    }
    uint8 Digest[16];
    Hash.Final(Digest);
    return BytesToHex(Digest, UE_ARRAY_COUNT(Digest));
}

void CaptureRecoveryMemory(const FJson& Receipt, const TCHAR* Field)
{
    const FJson Memory = MakeShared<FJsonObject>();
    for (const FString& Name : RecoveryPackages(Receipt))
    {
        UPackage* Package = FindPackage(nullptr, *Name);
        Memory->SetStringField(Name, Package ? MemoryDigest(Package) : TEXT("absent"));
    }
    Receipt->SetObjectField(Field, Memory);
}

bool CheckRecoveryMemory(const FJson& Receipt, FString& Error)
{
    const bool bSameEpoch = Text(Receipt, TEXT("editor_epoch")) == EditorEpoch();
    const FJson Before = Object(Receipt, TEXT("package_memory_before"));
    const FJson Applied = Object(Receipt, TEXT("package_memory_applied"));
    const FJson Evidence = MakeShared<FJsonObject>();
    Receipt->SetObjectField(TEXT("recovery_memory"), Evidence);
    for (const FString& Name : RecoveryPackages(Receipt))
    {
        UPackage* Package = FindPackage(nullptr, *Name);
        const FJson Row = MakeShared<FJsonObject>();
        Row->SetBoolField(TEXT("loaded"), Package != nullptr);
        Row->SetBoolField(TEXT("dirty"), Package && Package->IsDirty());
        Evidence->SetObjectField(Name, Row);
        if (!Package)
        {
            continue; // Disk evidence is checked separately before any restore.
        }
        if (!bSameEpoch && !Package->IsDirty())
        {
            continue; // Clean disk reload need not match an unsaved checkpoint.
        }
        const FString Current = MemoryDigest(Package);
        Row->SetStringField(TEXT("digest"), Current);
        if (Current.IsEmpty() || (Current != Text(Before, *Name) && Current != Text(Applied, *Name)))
        {
            Error = TEXT("unrecorded package memory must be preserved: ") + Name;
            return false;
        }
    }
    const FJson Request = Object(Receipt, TEXT("request"));
    if (Flag(Request, TEXT("expected_absent")))
    {
        const FJson Current = ResultSnapshot(Request, Object(Receipt, TEXT("response")));
        if (!Current.IsValid() || (Flag(Current, TEXT("exists"), true)
            && (Text(Current, TEXT("content_revision")).IsEmpty()
                || Text(Current, TEXT("content_revision")) != Text(Object(Receipt, TEXT("applied")), TEXT("content_revision")))))
        {
            Error = TEXT("new asset no longer matches the recorded creation");
            return false;
        }
    }
    return true;
}
}
