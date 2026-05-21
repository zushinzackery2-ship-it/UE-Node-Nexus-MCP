#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustom.h"

class FJsonValue;

namespace UeNodeNexusBridge
{
FString MaterialCustomJsonValueToCompactText(const TSharedPtr<FJsonValue>& Value);
void RefreshMaterialCustomExpressionPins(UMaterialExpressionCustom* Custom);
bool ReadMaterialCustomOutputType(const TSharedPtr<FJsonObject>& Object, ECustomMaterialOutputType& OutType);

bool ApplyMaterialCustomInputsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason);
bool ApplyMaterialCustomAdditionalOutputsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason);
bool ApplyMaterialCustomAdditionalDefinesJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason);
bool ApplyMaterialCustomIncludeFilePathsJson(UMaterialExpressionCustom* Custom, const TSharedPtr<FJsonValue>& Value, bool bDryRun, FString& OutOldValue, FString& OutFailureReason);
}
