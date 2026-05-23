#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UMaterialExpression;
struct FExpressionInput;

namespace UeNodeNexusBridge
{
struct FMaterialPatchContext;

TSharedPtr<FJsonObject> MakeMaterialPatchLinkJson(const FString& FromNodeId, const FString& FromPinId, const FString& ToNodeId, const FString& ToPinId);
void AddMaterialPatchDiagnostic(TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FString& Code, const FString& Message, UMaterial* Material);
FString ReadMaterialPatchNodeRef(const TSharedPtr<FJsonObject>& Op, const FString& CanonicalField, const FString& AliasField);
UMaterialExpression* ResolveMaterialPatchNode(UMaterial* Material, const FString& NodeId, const FMaterialPatchContext& Context);
bool IsMaterialPatchOutputRef(const FString& NodeId);
FString MaterialPatchValueTypeName(uint32 Type);
}
