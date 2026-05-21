#pragma once

#include "CoreMinimal.h"

class UMaterialExpression;

namespace UeNodeNexusBridge
{
struct FMaterialPatchContext
{
    TMap<FString, UMaterialExpression*> ClientNodes;
};
}
