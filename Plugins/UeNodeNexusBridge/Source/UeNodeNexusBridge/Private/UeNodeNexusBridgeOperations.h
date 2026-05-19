#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> DispatchOperation(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAssetList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAssetGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAssetCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleLevelCurrentGet(const FString& Operation, const FString& RequestId);
TSharedPtr<FJsonObject> HandleLevelActorsList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleBlueprintDetailsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAnimBlueprintSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleDiagnosticsGet(const FString& Operation, const FString& RequestId);
TSharedPtr<FJsonObject> HandleAssetCompile(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAssetValidate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleAssetSave(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleGraphSnapshotGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleGraphPatchApply(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNodeParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNodeParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialInstanceParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMaterialInstanceParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
}
