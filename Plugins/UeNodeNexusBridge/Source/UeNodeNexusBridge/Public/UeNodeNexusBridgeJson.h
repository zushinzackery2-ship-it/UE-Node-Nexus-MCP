#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Templates/UniquePtr.h"

struct FHttpServerResponse;

namespace UeNodeNexusBridge
{
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeEnvelope(const FString& Operation, const FString& RequestId, bool bOk);
UENODENEXUSBRIDGE_API TUniquePtr<FHttpServerResponse> JsonResponse(const TSharedPtr<FJsonObject>& JsonObject);
UENODENEXUSBRIDGE_API FString BodyToString(const TArray<uint8>& Body);
UENODENEXUSBRIDGE_API bool TryGetPayload(const TSharedPtr<FJsonObject>& Envelope, TSharedPtr<FJsonObject>& OutPayload);
UENODENEXUSBRIDGE_API int32 ReadCursor(const TSharedPtr<FJsonObject>& Payload);
UENODENEXUSBRIDGE_API int32 ReadLimit(const TSharedPtr<FJsonObject>& Payload, int32 DefaultLimit, int32 MaxLimit);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message, const FString& AssetPath, const FString& Source);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeEmptyDiff();
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeDirtyState(UObject* Asset);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakePinIntegrity(bool bOk, const TArray<TSharedPtr<FJsonValue>>& BrokenLinks, const TArray<TSharedPtr<FJsonValue>>& MissingPins);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeCompilePostCheck(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState);
UENODENEXUSBRIDGE_API TSharedPtr<FJsonObject> MakeWriteDataWithDiffFormat(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState, const FString& DiffFormat);
UENODENEXUSBRIDGE_API void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text);
}
