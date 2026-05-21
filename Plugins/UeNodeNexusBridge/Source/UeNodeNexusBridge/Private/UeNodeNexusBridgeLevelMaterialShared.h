#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class AActor;
class UMeshComponent;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeInvalidLevelMaterialResponse(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message);
UMeshComponent* ResolveMeshComponent(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId);
TSharedPtr<FJsonObject> MeshComponentToJson(AActor* Actor, UMeshComponent* Component, bool bIncludeMaterials);
}
