#pragma once

#include "CoreMinimal.h"
#include "SceneTypes.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UMaterialExpression;
class UMaterialFunction;
struct FExpressionInput;

namespace UeNodeNexusBridge
{
FString MaterialExpressionNodeId(UMaterialExpression* Expression);
FString MaterialOutputNodeId();
bool IsMaterialOutputNodeId(const FString& NodeId);
TArray<EMaterialProperty> MaterialOutputProperties();
FString MaterialOutputPropertyName(EMaterialProperty Property);
bool ResolveMaterialOutputProperty(const FString& PinId, EMaterialProperty& OutProperty);
FExpressionInput* ResolveMaterialOutputInput(UMaterial* Material, const FString& PinId);
UMaterialExpression* FindMaterialExpression(UMaterial* Material, const FString& NodeId);
UClass* ResolveMaterialExpressionClass(const FString& NodeClass);
TArray<TSharedPtr<FJsonValue>> BuildMaterialExpressionParams(UMaterialExpression* Expression);
TArray<TSharedPtr<FJsonValue>> BuildMaterialOutputParams(UMaterial* Material);
bool TrySetMaterialSyntheticParam(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bApply, FString& OutOldValue);
bool ParseMaterialPinId(const FString& PinId, bool& bOutInput, int32& OutIndex);
FExpressionInput* FindMaterialInput(UMaterialExpression* Expression, const FString& PinId);
FExpressionInput* ResolveMaterialInputPin(UMaterialExpression* Expression, const FString& PinId);
bool ResolveMaterialOutputPin(UMaterialExpression* Expression, const FString& PinId, bool& bOutInput, int32& OutIndex);
FString DescribeMaterialOutputPins(UMaterialExpression* Expression);
FString FindMaterialInputName(UMaterialExpression* Expression, const FString& PinId);
FString FindMaterialOutputName(UMaterialExpression* Expression, const FString& PinId);
bool ReadMaterialPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY);
void AppendMaterialDiff(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item);
void AddMaterialParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After);
bool ApplyMaterialExpressionParamValue(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff);
bool ApplyMaterialExpressionParamJsonValue(UMaterial* Material, UMaterialExpression* Expression, const FString& Name, const TSharedPtr<FJsonValue>& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutFailureReason);
bool ApplyMaterialOutputParamValue(UMaterial* Material, const FString& Name, const FString& Value, bool bDryRun, TSharedPtr<FJsonObject> Diff);
TSharedPtr<FJsonObject> BuildMaterialPinIntegrity(UMaterial* Material);
TSharedPtr<FJsonObject> BuildMaterialFunctionPinIntegrity(UMaterialFunction* Function);
TSharedPtr<FJsonObject> BuildMaterialOutputInterfaceData(UMaterial* Material, const TSharedPtr<FJsonObject>& Payload);
}
