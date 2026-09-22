#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UObject;

namespace UeNodeNexusBridge::Transcode
{
// The asset a plan asks to have created, decided before anything is written.
// Check runs in dry run and Create runs for real against the same rules, so a
// preview that promises a create is a promise the publish can keep. Check also
// hands back the parsed names so the caller never parses the path twice.
bool CheckAssetForKind(
    const FString& AssetPath,
    const FString& Kind,
    const TArray<TSharedPtr<FJsonValue>>& Plan,
    const FString& AssetClass,
    FString& OutPackageName,
    FString& OutAssetName,
    FString& OutError);

UObject* CreateAssetForKind(
    const FString& AssetPath,
    const FString& Kind,
    const TArray<TSharedPtr<FJsonValue>>& Plan,
    const FString& AssetClass,
    FString& OutError);
}
