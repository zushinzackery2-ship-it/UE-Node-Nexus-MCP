#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class FJsonValue;

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;
void RefreshFunctionCalls(UBlueprint* Blueprint, const TSet<FName>& Functions);
void ApplyGraphDeletions(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan,
    FApplyContext& Context, TArray<UEdGraph*>& Touched);
void ApplyScheduledGraphOps(UBlueprint* Blueprint, const TArray<TSharedPtr<FJsonValue>>& Plan,
    const TArray<int32>& Indices, FApplyContext& Context, TArray<UEdGraph*>& Touched);
}
