#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UObject;

namespace UeNodeNexusBridge::Collaboration
{
using FJson = TSharedPtr<FJsonObject>;
// Exports one live asset; Observe resolves the asset and answers for a missing one.
using FObserver = TFunction<FJson(const FJson& Request, UObject* Asset)>;
using FCommitBody = TFunction<FJson(const FJson&)>;

UENODENEXUSBRIDGE_API FString EditorEpoch();
UENODENEXUSBRIDGE_API FString ContentDigest(const FJson& Json);
UENODENEXUSBRIDGE_API FJson StampRaw(const FJson& Raw);
UENODENEXUSBRIDGE_API FJson Observe(const FJson& Request);
UENODENEXUSBRIDGE_API void RegisterObserver(const FString& Kind, FObserver Observer);
UENODENEXUSBRIDGE_API void UnregisterObserver(const FString& Kind);
UENODENEXUSBRIDGE_API bool BindRepository(const FString& Repository, const FString& ProjectId, FString& Error);
UENODENEXUSBRIDGE_API FString BoundRepository();
UENODENEXUSBRIDGE_API FJson RunCommit(const FString& Operation, const FString& RequestId, const FJson& Payload, FCommitBody Body);
UENODENEXUSBRIDGE_API FJson Recover(const FString& Operation, const FString& RequestId, const FJson& Payload);
UENODENEXUSBRIDGE_API FJson DeleteAsset(const FString& Operation, const FString& RequestId, const FJson& Payload);
}
