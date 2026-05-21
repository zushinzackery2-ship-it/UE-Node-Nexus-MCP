#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

struct FHttpServerResponse;
class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeError(const FString& Code, const FString& Message);
TSharedPtr<FJsonObject> MakeEnvelope(const FString& Operation, const FString& RequestId, bool bOk);
TUniquePtr<FHttpServerResponse> JsonResponse(const TSharedPtr<FJsonObject>& JsonObject);
FString BodyToString(const TArray<uint8>& Body);
bool TryGetPayload(const TSharedPtr<FJsonObject>& Envelope, TSharedPtr<FJsonObject>& OutPayload);
int32 ReadCursor(const TSharedPtr<FJsonObject>& Payload);
int32 ReadLimit(const TSharedPtr<FJsonObject>& Payload, int32 DefaultLimit, int32 MaxLimit);
TSharedPtr<FJsonObject> MakeDiagnostic(const FString& Severity, const FString& Code, const FString& Message, const FString& AssetPath, const FString& Source);
TSharedPtr<FJsonObject> MakeEmptyDiff();
TSharedPtr<FJsonObject> MakeDirtyState(UObject* Asset);
TSharedPtr<FJsonObject> MakePinIntegrity(bool bOk, const TArray<TSharedPtr<FJsonValue>>& BrokenLinks, const TArray<TSharedPtr<FJsonValue>>& MissingPins);
TSharedPtr<FJsonObject> MakeCompilePostCheck(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount);
TSharedPtr<FJsonObject> MakeWriteData(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState);
TSharedPtr<FJsonObject> MakeWriteDataWithDiffFormat(bool bDryRun, bool bApplied, bool bChanged, const TSharedPtr<FJsonObject>& Diff, const TSharedPtr<FJsonObject>& PinIntegrity, const TSharedPtr<FJsonObject>& Compile, const TSharedPtr<FJsonObject>& DirtyState, const FString& DiffFormat);
void SetTextPayload(const TSharedPtr<FJsonObject>& Data, const FString& Text);
}
