#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;

namespace UeNodeNexusBridge
{
bool ReadAssetBoolField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, bool DefaultValue);
TSharedPtr<FJsonObject> MakeInvalidAssetRequest(const FString& Operation, const FString& RequestId, const FString& Message);
TSharedPtr<FJsonObject> MakeAssetNotFoundResponse(const FString& Operation, const FString& RequestId, const FString& AssetPath);
UObject* LoadAssetForManagement(const FString& AssetPath);
bool SplitAssetObjectPath(const FString& ObjectPath, FString& OutPackageName, FString& OutPackagePath, FString& OutAssetName, FText& OutReason);
TSharedPtr<FJsonObject> MakeAssetOpDiff(const FString& FieldName, const TSharedPtr<FJsonObject>& Item);
TSharedPtr<FJsonObject> MakeAssetWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const FString& PackageName, bool bDirty, bool bSaved);
TArray<TSharedPtr<FJsonValue>> PackageNamesToJson(const TArray<FName>& PackageNames);
struct FRedirectorFixupProgress
{
    TArray<TSharedPtr<FJsonValue>> Redirectors;
    TArray<TSharedPtr<FJsonValue>> RedirectorPackages;
    TArray<TSharedPtr<FJsonValue>> PackageLoads;
    TArray<TSharedPtr<FJsonValue>> Referencers;
    TArray<TSharedPtr<FJsonValue>> IncompleteReferencers;
    TArray<TSharedPtr<FJsonValue>> ModifiedPackages;
    TArray<TSharedPtr<FJsonValue>> SavedPackages;
    TArray<TSharedPtr<FJsonValue>> FailedPackages;
    TArray<TSharedPtr<FJsonValue>> RemainingReferencers;
    TArray<TSharedPtr<FJsonValue>> DeletedRedirectors;
    TArray<TSharedPtr<FJsonValue>> DeletedPackages;
    int32 Count = 0;
    bool bDirty = false;
    FString Error;
};
// Fixes every redirector under the folder as one package-bounded transaction:
// all redirector packages and referencers are loaded and verified before any
// referencer is saved, and nothing is deleted until no referencer remains.
FRedirectorFixupProgress FixRedirectorsUnderFolder(const FString& FolderPath, bool bDryRun);
FRedirectorFixupProgress FixRedirectorPackages(const TArray<FString>& PackageNames, bool bDryRun);
}
