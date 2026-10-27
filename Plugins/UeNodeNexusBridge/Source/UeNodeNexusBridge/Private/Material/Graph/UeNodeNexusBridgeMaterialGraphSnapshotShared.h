#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterialExpression;

namespace UeNodeNexusBridge
{
struct FCompactGraphBuilder;

enum class EMaterialSnapshotNodeParamsFormat : uint8
{
    Compact,
    Full,
};

FString MaterialSnapshotNodeId(const UMaterialExpression* Expression);
FString MaterialSnapshotFunctionExpressionDisplayName(UMaterialExpression* Expression);
TSharedPtr<FJsonObject> MaterialSnapshotNodeToJson(UMaterialExpression* Expression, bool bIncludeNodeParams, EMaterialSnapshotNodeParamsFormat NodeParamsFormat);
void AddMaterialSnapshotLinks(UMaterialExpression* TargetExpression, TArray<TSharedPtr<FJsonValue>>& Links);
void AppendMaterialSnapshotCompactNode(FCompactGraphBuilder& Builder, UMaterialExpression* Expression, bool bIncludeNodeParams);
void AddMaterialSnapshotCompactLinks(FCompactGraphBuilder& Builder, UMaterialExpression* TargetExpression);
}
