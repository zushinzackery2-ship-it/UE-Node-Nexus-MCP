#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
void AppendDiffItem(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item);
void AddGraphParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After);
bool ReadGraphPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY);
bool ReadJsonScalarAsString(const TSharedPtr<FJsonObject>& Json, const FString& Field, FString& OutValue);
}
