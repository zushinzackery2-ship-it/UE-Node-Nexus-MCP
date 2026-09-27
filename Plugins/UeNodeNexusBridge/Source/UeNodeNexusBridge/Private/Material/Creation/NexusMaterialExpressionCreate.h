#pragma once

#include "CoreMinimal.h"

class UMaterialExpression;

namespace UeNodeNexusBridge
{
UMaterialExpression* CreateNexusMaterialExpression(UObject* Owner, UClass* Class, int32 X, int32 Y);
}
