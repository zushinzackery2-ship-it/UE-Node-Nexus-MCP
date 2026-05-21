#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
FString ShortMaterialExpressionClass(UMaterialExpression* Expression);
FString ReadObjectPropertyText(UObject* Object, const FName& Name);
FString MaterialNodeAlias(UMaterial* Material, UMaterialExpression* Target);
FString MaterialNodeAlias(UMaterialFunction* Function, UMaterialExpression* Target);
UMaterialExpression* ResolveMaterialInterfaceNode(UMaterial* Material, const FString& NodeId);
UMaterialExpression* ResolveMaterialInterfaceNode(UMaterialFunction* Function, const FString& NodeId);
FString MaterialOutputName(UMaterialExpression* Expression, int32 Index);
}
