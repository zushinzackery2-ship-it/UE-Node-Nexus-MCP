#include "UeNodeNexusBridgeDiagnostics.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"
#include "Runtime/NexusRuntimeDiagnostics.h"

namespace UeNodeNexusBridge
{
namespace
{
struct FDiagnosticsRequest
{
    FString AssetPath;
    FString Severity = TEXT("all");
    int32 Limit = TNumericLimits<int32>::Max();
};

bool SeverityMatches(const FString& ActualSeverity, const FString& RequestedSeverity)
{
    if (RequestedSeverity.IsEmpty() || RequestedSeverity.Equals(TEXT("all"), ESearchCase::IgnoreCase))
    {
        return true;
    }
    if (RequestedSeverity.Equals(TEXT("error"), ESearchCase::IgnoreCase))
    {
        return ActualSeverity.Equals(TEXT("error"), ESearchCase::IgnoreCase) || ActualSeverity.Equals(TEXT("fatal"), ESearchCase::IgnoreCase);
    }
    return ActualSeverity.Equals(RequestedSeverity, ESearchCase::IgnoreCase);
}

bool ReadDiagnosticSeverity(const TSharedPtr<FJsonObject>& Diagnostic, FString& OutSeverity)
{
    return Diagnostic.IsValid() && Diagnostic->TryGetStringField(TEXT("severity"), OutSeverity);
}

bool DiagnosticMatchesRequest(const TSharedPtr<FJsonObject>& Diagnostic, const FDiagnosticsRequest& Request)
{
    FString Severity;
    if (!ReadDiagnosticSeverity(Diagnostic, Severity) || !SeverityMatches(Severity, Request.Severity))
    {
        return false;
    }

    if (!Request.AssetPath.IsEmpty())
    {
        FString DiagnosticAssetPath;
        if (!Diagnostic->TryGetStringField(TEXT("asset_path"), DiagnosticAssetPath) || !DiagnosticAssetPath.Equals(Request.AssetPath, ESearchCase::IgnoreCase))
        {
            return false;
        }
    }
    return true;
}

void AddDiagnosticIfMatches(
    const TSharedPtr<FJsonObject>& Diagnostic,
    const FDiagnosticsRequest& Request,
    TArray<TSharedPtr<FJsonValue>>& OutDiagnostics)
{
    if (OutDiagnostics.Num() >= Request.Limit || !DiagnosticMatchesRequest(Diagnostic, Request))
    {
        return;
    }
    OutDiagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
}

FDiagnosticsRequest ReadDiagnosticsRequest(const TSharedPtr<FJsonObject>& Payload)
{
    FDiagnosticsRequest Request;
    if (Payload.IsValid())
    {
        Payload->TryGetStringField(TEXT("asset_path"), Request.AssetPath);
        Payload->TryGetStringField(TEXT("severity"), Request.Severity);
        if (Payload->HasField(TEXT("limit")))
        {
            Request.Limit = ReadLimit(Payload, 200, 2000);
        }
    }
    if (Request.Severity.IsEmpty())
    {
        Request.Severity = TEXT("all");
    }
    return Request;
}

FString NormalizeAssetObjectPath(const FString& AssetPath)
{
    if (AssetPath.IsEmpty())
    {
        return FString();
    }

    FText Reason;
    const int32 LastSlashIndex = AssetPath.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    const int32 LastDotIndex = AssetPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (LastDotIndex > LastSlashIndex && FPackageName::IsValidObjectPath(AssetPath, &Reason))
    {
        return AssetPath;
    }
    if (FPackageName::IsValidLongPackageName(AssetPath, false, &Reason))
    {
        return AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    }
    return AssetPath;
}

bool IsSupportedDiagnosticAssetClass(const FAssetData& AssetData)
{
    const UClass* Class = FindObject<UClass>(nullptr, *AssetData.AssetClassPath.ToString());
    return Class && (Class->IsChildOf(UBlueprint::StaticClass())
        || Class->IsChildOf(UMaterialInterface::StaticClass())
        || Class->IsChildOf(UMaterialFunction::StaticClass()));
}

bool TryAddTargetAssetData(const FString& AssetPath, TArray<FAssetData>& OutAssets)
{
    const FString NormalizedAssetPath = NormalizeAssetObjectPath(AssetPath);
    const FString PackageName = FPackageName::ObjectPathToPackageName(NormalizedAssetPath);
    if (PackageName.IsEmpty())
    {
        return false;
    }

    FAssetData AssetData = FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(FSoftObjectPath(NormalizedAssetPath));
    if (!AssetData.IsValid())
    {
        UObject* LoadedAsset = FindObject<UObject>(nullptr, *NormalizedAssetPath);
        if (LoadedAsset != nullptr)
        {
            AssetData = FAssetData(LoadedAsset);
        }
    }
    if (!AssetData.IsValid())
    {
        return false;
    }

    OutAssets.Add(AssetData);
    return true;
}

void CollectDiagnosticAssetData(const FDiagnosticsRequest& Request, TArray<FAssetData>& OutAssets)
{
    if (!Request.AssetPath.IsEmpty())
    {
        TryAddTargetAssetData(Request.AssetPath, OutAssets);
        return;
    }

    FARFilter Filter;
    Filter.PackagePaths.Add(TEXT("/Game"));
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialInterface::StaticClass()->GetClassPathName());
    Filter.ClassPaths.Add(UMaterialFunction::StaticClass()->GetClassPathName());
    FAssetRegistryModule::GetRegistry().GetAssets(Filter, OutAssets);
}

void CollectAssetDiagnostics(const FDiagnosticsRequest& Request, FBridgeDiagnosticsResult& OutResult)
{
    TArray<FAssetData> Assets;
    CollectDiagnosticAssetData(Request, Assets);

    if (!Request.AssetPath.IsEmpty() && Assets.Num() == 0)
    {
        OutResult.bOk = false;
        OutResult.ErrorCode = TEXT("asset_not_found");
        OutResult.ErrorMessage = TEXT("Asset could not be found for diagnostics_get");
        return;
    }

    for (const FAssetData& AssetData : Assets)
    {
        if (OutResult.Diagnostics.Num() >= Request.Limit)
        {
            break;
        }

        ++OutResult.AssetsScanned;
        if (!IsSupportedDiagnosticAssetClass(AssetData))
        {
            ++OutResult.AssetsUnsupported;
            continue;
        }

        UObject* Asset = FindObject<UObject>(nullptr, *AssetData.GetObjectPathString());
        if (Asset == nullptr)
        {
            ++OutResult.AssetsNotLoaded;
            continue;
        }

        const FString AssetPath = AssetData.GetObjectPathString();
        FBridgeAssetCompileDiagnostics CompileDiagnostics = InspectAssetDiagnostics(Asset, AssetPath);
        if (!CompileDiagnostics.bSupported)
        {
            ++OutResult.AssetsUnsupported;
            continue;
        }

        ++OutResult.AssetsSupported;
        for (const TSharedPtr<FJsonValue>& DiagnosticValue : CompileDiagnostics.Diagnostics)
        {
            if (OutResult.Diagnostics.Num() >= Request.Limit)
            {
                break;
            }

            const TSharedPtr<FJsonObject> Diagnostic = DiagnosticValue.IsValid() ? DiagnosticValue->AsObject() : nullptr;
            AddDiagnosticIfMatches(Diagnostic, Request, OutResult.Diagnostics);
        }
    }
}
}

FBridgeDiagnosticsResult CollectBridgeDiagnostics(const TSharedPtr<FJsonObject>& Payload)
{
    FDiagnosticsRequest Request = ReadDiagnosticsRequest(Payload);
    Request.AssetPath = NormalizeAssetObjectPath(Request.AssetPath);

    FBridgeDiagnosticsResult Result;
    Result.Scope = Request.AssetPath.IsEmpty() ? TEXT("project") : Request.AssetPath;
    Payload->TryGetBoolField(TEXT("include_assets"), Result.bInspectAssets);
    if (Result.bInspectAssets)
    {
        CollectAssetDiagnostics(Request, Result);
    }
    auto RuntimePayload = MakeShared<FJsonObject>(*Payload);
    RuntimePayload->SetStringField(TEXT("asset_path"), Request.AssetPath);
    Result.Runtime = RuntimeDiagnostics::Read(RuntimePayload);
    for (const auto& Value : Result.Runtime->GetArrayField(TEXT("items")))
    {
        Result.Diagnostics.Add(Value);
    }
    Result.Runtime->RemoveField(TEXT("items"));
    if (Payload->HasField(TEXT("session_id")) && !Result.Runtime->GetBoolField(TEXT("available")))
    {
        Result.bOk = false;
        Result.ErrorCode = TEXT("runtime_session_not_found");
        Result.ErrorMessage = TEXT("Runtime diagnostic session is outside retained history");
    }
    return Result;
}
}
