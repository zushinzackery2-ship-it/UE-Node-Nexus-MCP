#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UMaterial;
class UMaterialExpression;
struct FExpressionInput;

namespace UeNodeNexusBridge
{
FString MaterialExpressionNodeId(UMaterialExpression* Expression);
UMaterialExpression* FindMaterialExpression(UMaterial* Material, const FString& NodeId);
bool ParseMaterialPinId(const FString& PinId, bool& bOutInput, int32& OutIndex);
FExpressionInput* FindMaterialInput(UMaterialExpression* Expression, const FString& PinId);
FString FindMaterialInputName(UMaterialExpression* Expression, const FString& PinId);
FString FindMaterialOutputName(UMaterialExpression* Expression, const FString& PinId);
bool ReadMaterialPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY);
void AppendMaterialDiff(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item);
void AddMaterialParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After);
TSharedPtr<FJsonObject> BuildMaterialPinIntegrity(UMaterial* Material);
}
