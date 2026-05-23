#pragma once

#include "CoreMinimal.h"

class UMaterialExpression;

namespace UeNodeNexusBridge
{
struct FMaterialFunctionPatchContext
{
    TMap<FString, UMaterialExpression*> ClientNodes;
};
}
