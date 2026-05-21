#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterial;
class UMaterialExpression;

namespace UeNodeNexusBridge
{
bool ApplyMaterialCustomJsonParam(
    UMaterial* Material,
    UMaterialExpression* Expression,
    const FString& Name,
    const TSharedPtr<FJsonValue>& Value,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    FString& OutFailureReason);
}
