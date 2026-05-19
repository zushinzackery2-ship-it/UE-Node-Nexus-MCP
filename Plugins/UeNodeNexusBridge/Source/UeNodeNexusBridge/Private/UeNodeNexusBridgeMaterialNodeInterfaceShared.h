#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialExpression;

namespace UeNodeNexusBridge
{
FString ShortMaterialExpressionClass(UMaterialExpression* Expression);
FString ReadObjectPropertyText(UObject* Object, const FName& Name);
FString MaterialNodeAlias(UMaterial* Material, UMaterialExpression* Target);
UMaterialExpression* ResolveMaterialInterfaceNode(UMaterial* Material, const FString& NodeId);
FString MaterialOutputName(UMaterialExpression* Expression, int32 Index);
}
