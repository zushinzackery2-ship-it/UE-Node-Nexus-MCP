#pragma once

#include "CoreMinimal.h"
#include "SceneTypes.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UMaterialExpression;
struct FExpressionInput;

namespace UeNodeNexusBridge
{
FString MaterialExpressionNodeId(UMaterialExpression* Expression);
FString MaterialOutputNodeId();
bool IsMaterialOutputNodeId(const FString& NodeId);
TArray<EMaterialProperty> MaterialOutputProperties();
FString MaterialOutputPropertyName(EMaterialProperty Property);
FExpressionInput* ResolveMaterialOutputInput(UMaterial* Material, const FString& PinId);
UMaterialExpression* FindMaterialExpression(UMaterial* Material, const FString& NodeId);
UClass* ResolveMaterialExpressionClass(const FString& NodeClass);
TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParams(UMaterialExpression* Expression);
bool TrySetMaterialSyntheticParam(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bApply, FString& OutOldValue);
bool ParseMaterialPinId(const FString& PinId, bool& bOutInput, int32& OutIndex);
FExpressionInput* FindMaterialInput(UMaterialExpression* Expression, const FString& PinId);
FExpressionInput* ResolveMaterialInputPin(UMaterialExpression* Expression, const FString& PinId);
bool ResolveMaterialOutputPin(UMaterialExpression* Expression, const FString& PinId, bool& bOutInput, int32& OutIndex);
FString FindMaterialInputName(UMaterialExpression* Expression, const FString& PinId);
FString FindMaterialOutputName(UMaterialExpression* Expression, const FString& PinId);
bool ReadMaterialPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY);
void AppendMaterialDiff(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item);
void AddMaterialParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After);
bool ApplyMaterialExpressionParamValue(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff);
TSharedPtr<FJsonObject> BuildMaterialPinIntegrity(UMaterial* Material);
TSharedPtr<FJsonObject> BuildMaterialOutputInterfaceData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
}
