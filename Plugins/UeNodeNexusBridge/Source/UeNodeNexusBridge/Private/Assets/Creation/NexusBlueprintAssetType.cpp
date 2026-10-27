#include "NexusBlueprintAssetType.h"

#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/Interface.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FBlueprintKind
{
    const TCHAR* Name;
    EBlueprintType Type;
    bool bCreatable;
};

// Mirrors the engine's Blueprint factories: each is a fixed pair of a default
// parent class and a BlueprintType, and it is the pair that makes the asset
// legal.
const FBlueprintKind Kinds[] =
{
    { TEXT("normal"), BPTYPE_Normal, true },
    { TEXT("const"), BPTYPE_Const, true },
    { TEXT("macro_library"), BPTYPE_MacroLibrary, true },
    { TEXT("interface"), BPTYPE_Interface, true },
    { TEXT("function_library"), BPTYPE_FunctionLibrary, true },
    // A level script belongs to its map and is never a standalone asset. It is
    // named here only so that every exported Blueprint has a printable type.
    { TEXT("level_script"), BPTYPE_LevelScript, false },
};

UClass* DefaultParent(EBlueprintType Type)
{
    switch (Type)
    {
    case BPTYPE_Interface:
        return UInterface::StaticClass();
    case BPTYPE_FunctionLibrary:
        return UBlueprintFunctionLibrary::StaticClass();
    default:
        return AActor::StaticClass();
    }
}

// What the parent class alone says the Blueprint has to be. Neither library
// base can produce a BPTYPE_Normal Blueprint, so those two decide; everything
// else is Normal and can still be overridden explicitly.
EBlueprintType InferredType(const UClass* ParentClass)
{
    if (ParentClass == UBlueprintFunctionLibrary::StaticClass())
    {
        return BPTYPE_FunctionLibrary;
    }
    if (ParentClass->IsChildOf(UInterface::StaticClass()))
    {
        return BPTYPE_Interface;
    }
    return BPTYPE_Normal;
}

bool ParseType(const FString& Requested, EBlueprintType& OutType, FString& OutError)
{
    for (const FBlueprintKind& Kind : Kinds)
    {
        if (!Requested.Equals(Kind.Name, ESearchCase::IgnoreCase))
        {
            continue;
        }
        if (!Kind.bCreatable)
        {
            OutError = FString::Printf(TEXT("blueprint_type '%s' cannot be created as a standalone asset"), *Requested);
            return false;
        }
        OutType = Kind.Type;
        return true;
    }
    OutError = FString::Printf(TEXT("unknown blueprint_type '%s'; expected one of: %s"),
        *Requested, *FString::Join(BlueprintTypeNames(), TEXT(", ")));
    return false;
}

// The parent each type accepts, stated the way the engine's own factories state
// it. UBlueprintFunctionLibraryFactory does not call CanCreateBlueprintOfClass -
// UBlueprintFunctionLibrary is not Blueprintable and would fail it - it demands
// the exact class instead, so that is what a function library is checked against.
bool CheckParent(EBlueprintType Type, const UClass* ParentClass, FString& OutError)
{
    const bool bInterfaceParent = ParentClass->IsChildOf(UInterface::StaticClass());
    if (Type == BPTYPE_FunctionLibrary)
    {
        if (ParentClass != UBlueprintFunctionLibrary::StaticClass())
        {
            OutError = FString::Printf(
                TEXT("a function library must derive directly from BlueprintFunctionLibrary, not '%s'"),
                *ParentClass->GetPathName());
            return false;
        }
        return true;
    }
    if (Type == BPTYPE_Interface && !bInterfaceParent)
    {
        OutError = FString::Printf(TEXT("an interface must derive from Interface, not '%s'"), *ParentClass->GetPathName());
        return false;
    }
    if (Type != BPTYPE_Interface && bInterfaceParent)
    {
        OutError = FString::Printf(
            TEXT("'%s' is an interface class; create it with blueprint_type 'interface'"),
            *ParentClass->GetPathName());
        return false;
    }
    if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        OutError = FString::Printf(
            TEXT("class '%s' is not a Blueprint base (not Blueprintable, deprecated, or a skeleton class)"),
            *ParentClass->GetPathName());
        return false;
    }
    return true;
}

bool Resolve(const FString& ParentClassPath, const FString& RequestedType, UClass*& OutParent, EBlueprintType& OutType, FString& OutError)
{
    EBlueprintType Type = BPTYPE_Normal;
    const bool bExplicit = !RequestedType.IsEmpty();
    if (bExplicit && !ParseType(RequestedType, Type, OutError))
    {
        return false;
    }
    UClass* ParentClass = DefaultParent(Type);
    if (!ParentClassPath.IsEmpty())
    {
        ParentClass = LoadObject<UClass>(nullptr, *ParentClassPath);
        if (ParentClass == nullptr)
        {
            OutError = FString::Printf(TEXT("parent class could not be loaded: %s"), *ParentClassPath);
            return false;
        }
    }
    if (!bExplicit)
    {
        Type = InferredType(ParentClass);
    }
    if (!CheckParent(Type, ParentClass, OutError))
    {
        return false;
    }
    OutParent = ParentClass;
    OutType = Type;
    return true;
}
}

TArray<FString> BlueprintTypeNames()
{
    TArray<FString> Names;
    for (const FBlueprintKind& Kind : Kinds)
    {
        if (Kind.bCreatable)
        {
            Names.Add(Kind.Name);
        }
    }
    return Names;
}

FString BlueprintTypeName(const UBlueprint* Blueprint)
{
    if (Blueprint == nullptr)
    {
        return FString();
    }
    for (const FBlueprintKind& Kind : Kinds)
    {
        if (Blueprint->BlueprintType == Kind.Type)
        {
            return Kind.Name;
        }
    }
    return FString();
}

bool CheckBlueprintAsset(const FString& ParentClassPath, const FString& RequestedType, FString& OutError)
{
    UClass* ParentClass = nullptr;
    EBlueprintType Type = BPTYPE_Normal;
    return Resolve(ParentClassPath, RequestedType, ParentClass, Type, OutError);
}

UObject* CreateTypedBlueprintAsset(UPackage* Package, FName AssetName, const FString& ParentClassPath, const FString& RequestedType, FString& OutError)
{
    UClass* ParentClass = nullptr;
    EBlueprintType Type = BPTYPE_Normal;
    if (!Resolve(ParentClassPath, RequestedType, ParentClass, Type, OutError))
    {
        return nullptr;
    }
    // The four-argument overload asks the Kismet compiler which UBlueprint and
    // UBlueprintGeneratedClass subclasses the parent wants, so an animation or
    // widget parent gets its own Blueprint class instead of the plain one.
    UObject* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, AssetName, Type);
    if (Blueprint == nullptr)
    {
        OutError = FString::Printf(TEXT("CreateBlueprint produced nothing for parent '%s'"), *ParentClass->GetPathName());
    }
    return Blueprint;
}
}
