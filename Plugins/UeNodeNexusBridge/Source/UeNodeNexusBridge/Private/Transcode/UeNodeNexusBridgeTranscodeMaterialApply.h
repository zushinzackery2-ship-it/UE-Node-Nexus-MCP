#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UMaterialExpression;

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;

// Expressions of a UMaterial or UMaterialFunction owner.
TConstArrayView<TObjectPtr<UMaterialExpression>> OwnerExpressions(UObject* Owner);
UMaterialExpression* ResolveMaterialNode(UObject* Owner, const FApplyContext& Context, const FString& LocalId);
FString ReadMaterialOpString(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field, const FString& Default = FString());
// connect_pins / disconnect_pins (material output node is the implicit "out").
bool ApplyMaterialLink(UObject* Owner, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context, bool bConnect);
// refresh_function_calls: re-sync MaterialFunctionCall nodes that reference FunctionPath.
void RefreshMaterialFunctionCalls(UObject* Owner, const FString& FunctionPath, FApplyContext& Context);
}
