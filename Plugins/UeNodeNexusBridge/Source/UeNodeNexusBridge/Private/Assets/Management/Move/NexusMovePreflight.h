#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UObject/SoftObjectPath.h"

namespace UeNodeNexusBridge::AssetMove
{
struct FEntry
{
    FString Source;
    FString Destination;
    FString Folder;
    FString Name;
    UObject* Asset = nullptr;
};

bool Prepare(const TArray<TSharedPtr<FJsonValue>>& Items, TArray<FEntry>& Entries,
    TArray<UPackage*>& Packages, TMap<FSoftObjectPath, FSoftObjectPath>& Redirects,
    TSharedPtr<FJsonObject>& Details, FString& Error);
TArray<TSharedPtr<FJsonValue>> CdoReferences(const TMap<FSoftObjectPath, FSoftObjectPath>& Redirects);
}
