#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FProperty;
class UClass;
class UMaterialExpression;

namespace UeNodeNexusBridge
{
bool IsEditableMaterialExpressionProperty(FProperty* Property);
TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionClassParams(UClass* Class);
TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParams(UMaterialExpression* Expression);
TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParamValues(UMaterialExpression* Expression);
bool ImportMaterialExpressionPropertyText(UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, FString& OutOldValue);
bool MaterialExpressionJsonValueToPropertyText(UMaterialExpression* Expression, const FString& Name, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError);
}
