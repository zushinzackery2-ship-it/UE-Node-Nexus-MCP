#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Templates/UniquePtr.h"
#include "UObject/UObjectGlobals.h"

namespace UeNodeNexusBridge
{
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message);
// Overload carrying field-level error context (e.g. conflicting fields, valid
// ranges, expected/actual types). The base overload always seeds empty details.
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details);
// Canonical failed-envelope builder. Replaces the per-domain Make<Domain>Error
// wrappers that all shared the same body.
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeOperationError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message, const TSharedPtr<FJsonObject>& Details = nullptr);
// Stamps data.elapsed_ms from a FPlatformTime::Seconds() start mark. Used by
// every summary handler.
UENODENEXUSBRIDGE_API void AddElapsedMs(const TSharedPtr<FJsonObject>& Data, double StartSeconds);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeEnvelope(const FString& Operation, const FString& RequestId, bool bOk);

// Shared read-op preamble: require a non-empty asset_path then LoadObject<T>.
// On failure returns nullptr and fills OutErrorResponse with the canonical
// invalid_request / asset_not_found envelope; on success returns the asset.
template <typename TAsset>
TAsset* LoadAssetOrError(const TSharedPtr<FJsonObject>& Payload, const FString& Operation, const FString& RequestId, TSharedPtr<FJsonObject>& OutErrorResponse, const TCHAR* AssetTypeName)
{
    OutErrorResponse.Reset();
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutErrorResponse = MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("asset_path is required"));
        return nullptr;
    }

    TAsset* Asset = LoadObject<TAsset>(nullptr, *AssetPath);
    if (Asset == nullptr)
    {
        OutErrorResponse = MakeOperationError(Operation, RequestId, TEXT("asset_not_found"), FString::Printf(TEXT("%s could not be loaded"), AssetTypeName));
        return nullptr;
    }
    return Asset;
}
// Serialize a JSON object to a compact UTF-16 FString. Transport-neutral
// replacement for the former HTTP-coupled JsonResponse builder.
UENODENEXUSBRIDGE_API FString SerializeJsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject);
UENODENEXUSBRIDGE_API FString BodyToString(const TArray<uint8>& Body);
UENODENEXUSBRIDGE_API bool TryGetPayload(const TSharedPtr<FJsonObject>& Envelope, TSharedPtr<FJsonObject>& OutPayload);
UENODENEXUSBRIDGE_API int32 ReadCursor(const TSharedPtr<FJsonObject>& Payload);
UENODENEXUSBRIDGE_API int32 ReadLimit(const TSharedPtr<FJsonObject>& Payload, int32 DefaultLimit, int32 MaxLimit);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message, const FString& AssetPath, const FString& Source);
// Object->GetPathName() or empty string. Shared so per-file static copies do
// not collide when unity builds merge translation units.
UENODENEXUSBRIDGE_API FString ObjectPathOrEmpty(const UObject* Object);
// Reads the optional numeric "index" field used by line-selection payloads.
UENODENEXUSBRIDGE_API bool ReadPayloadIndex(const TSharedPtr<FJsonObject>& Payload, int32& OutIndex);
// Appends either the payload-selected line or all lines; false when the
// requested index is out of range.
UENODENEXUSBRIDGE_API bool AppendSelectedLines(FString& Text, const TArray<FString>& Lines, const TSharedPtr<FJsonObject>& Payload);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeEmptyDiff();
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeDirtyState(UObject* Asset);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakePinIntegrity(bool bOk, const TArray<TSharedPtr<FJsonValue>>& BrokenLinks, const TArray<TSharedPtr<FJsonValue>>& MissingPins);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeCompilePostCheck(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeWriteDataWithDiffFormat(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState, const FString& DiffFormat);
UENODENEXUSBRIDGE_API void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text);
}
