#pragma once

#include "CoreMinimal.h"
#include "Assets/Management/NexusPackageLoadAudit.h"
#include "UObject/SoftObjectPath.h"

class UObjectRedirector;

namespace UeNodeNexusBridge
{
struct FRedirectorFixupProgress;
}

namespace UeNodeNexusBridge::RedirectorFixup
{
struct FRedirectorPackage
{
    FName PackageName;
    TArray<UObjectRedirector*> Redirectors;
};

bool CollectRedirectorPackages(const FString& FolderPath, FRedirectorFixupProgress& Progress,
    TArray<FRedirectorPackage>& OutPackages);
bool ResolveRedirectorPackages(TArray<FRedirectorPackage>& Packages, const TArray<FPackageLoadAudit>& Audits,
    FRedirectorFixupProgress& Progress, TMap<FSoftObjectPath, FSoftObjectPath>& OutRedirects);
bool CollectReferencers(const TArray<FRedirectorPackage>& Packages, TArray<FString>& OutReferencers, FString& OutError);
bool RemainingReferencers(const TArray<FRedirectorPackage>& Packages, FRedirectorFixupProgress& Progress);
}
