#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UObject;
class UMaterialInterface;

namespace UeNodeNexusBridge
{
struct FBridgeAssetCompileDiagnostics
{
    bool bSupported = false;
    bool bRan = false;
    bool bOk = false;
    int32 ErrorCount = 0;
    int32 WarningCount = 0;
    FString AssetClass;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
};

struct FBridgeDiagnosticsResult
{
    bool bOk = true;
    FString ErrorCode;
    FString ErrorMessage;
    FString Scope;
    int32 AssetsScanned = 0;
    int32 AssetsSupported = 0;
    int32 AssetsUnsupported = 0;
    int32 AssetsFailedToLoad = 0;
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
};

FBridgeAssetCompileDiagnostics CollectAssetCompileDiagnostics(UObject* Asset, const FString& AssetPath, bool bMarkMaterialDirty);
FBridgeDiagnosticsResult CollectBridgeDiagnostics(const TSharedPtr<FJsonObject>& Payload);
TArray<FString> CollectMaterialCompileErrors(UMaterialInterface* MaterialInterface);
void AddMaterialCompileDiagnostics(const TArray<FString>& CompileErrors, const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& Diagnostics);
}
