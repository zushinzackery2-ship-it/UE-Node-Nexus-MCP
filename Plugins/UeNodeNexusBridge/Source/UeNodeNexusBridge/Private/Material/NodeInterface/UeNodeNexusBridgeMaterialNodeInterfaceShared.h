#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
class FJsonValue;

namespace UeNodeNexusBridge
{
FString ShortMaterialExpressionClass(UMaterialExpression* Expression);
FString ReadObjectPropertyText(UObject* Object, const FName& Name);
FString MaterialNodeAlias(UMaterial* Material, UMaterialExpression* Target);
FString MaterialNodeAlias(UMaterialFunction* Function, UMaterialExpression* Target);
UMaterialExpression* ResolveMaterialInterfaceNode(UMaterial* Material, const FString& NodeId);
UMaterialExpression* ResolveMaterialInterfaceNode(UMaterialFunction* Function, const FString& NodeId);
FString MaterialOutputName(UMaterialExpression* Expression, int32 Index);
TArray<TSharedPtr<FJsonValue>> BuildCompactParamRows(const TArray<TSharedPtr<FJsonValue>>& Params, const FString& ValueField);
TArray<TSharedPtr<FJsonValue>> BuildMaterialCompactInputRows(UMaterial* Material, UMaterialExpression* Expression);
TArray<TSharedPtr<FJsonValue>> BuildMaterialCompactOutputRows(UMaterial* Material, UMaterialExpression* Expression);
TArray<TSharedPtr<FJsonValue>> BuildMaterialFunctionCompactInputRows(UMaterialFunction* Function, UMaterialExpression* Expression);
TArray<TSharedPtr<FJsonValue>> BuildMaterialFunctionCompactOutputRows(UMaterialFunction* Function, UMaterialExpression* Expression);
}
