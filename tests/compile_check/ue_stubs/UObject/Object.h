// UObject family stubs for compiler-only checks; see CoreTypes.h.
#pragma once

#include "Containers/UnrealString.h"

class UClass;
class UPackage;

class UObject
{
public:
    virtual ~UObject() = default;

    FString GetName() const;
    FString GetPathName() const;
    UClass* GetClass() const;
    UPackage* GetOutermost() const;
    bool MarkPackageDirty() const;
    bool Modify(bool bAlwaysMarkDirty = true);

    static UClass* StaticClass();
};

class UClass : public UObject
{
public:
    bool IsChildOf(const UClass* SomeBase) const;

    static UClass* StaticClass();
};

class UPackage : public UObject
{
public:
    bool IsDirty() const;
};

template <typename ToType, typename FromType>
ToType* Cast(FromType* Source)
{
    return dynamic_cast<ToType*>(Source);
}

template <typename ClassType>
class TSubclassOf
{
public:
    TSubclassOf() = default;
    TSubclassOf(UClass* InClass) : ClassPtr(InClass) {}

    operator UClass*() const { return ClassPtr; }
    UClass* Get() const { return ClassPtr; }

private:
    UClass* ClassPtr = nullptr;
};
