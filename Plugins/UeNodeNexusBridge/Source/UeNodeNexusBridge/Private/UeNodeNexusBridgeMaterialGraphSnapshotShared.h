#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterialExpression;
struct FCompactGraphBuilder;

namespace UeNodeNexusBridge
{
FString MaterialSnapshotNodeId(const UMaterialExpression* Expression);
FString MaterialSnapshotFunctionExpressionDisplayName(UMaterialExpression* Expression);
TSharedPtr<FJsonObject> MaterialSnapshotNodeToJson(UMaterialExpression* Expression, bool bIncludeNodeParams);
void AddMaterialSnapshotLinks(UMaterialExpression* TargetExpression, TArray<TSharedPtr<FJsonValue>>& Links);
void AppendMaterialSnapshotCompactNode(FCompactGraphBuilder& Builder, UMaterialExpression* Expression, bool bIncludeNodeParams);
void AddMaterialSnapshotCompactLinks(FCompactGraphBuilder& Builder, UMaterialExpression* TargetExpression);
}
