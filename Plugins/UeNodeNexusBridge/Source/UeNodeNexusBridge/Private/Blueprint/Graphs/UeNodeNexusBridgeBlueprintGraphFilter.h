#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

namespace UeNodeNexusBridge
{
bool IsExecPin(const UEdGraphPin* Pin);

struct FBlueprintGraphFilter
{
    FString Keyword;
    TArray<FString> ClassFilter;
    FString TraceFrom;
    int32 TraceDepth = 3;
    bool bExecOnly = false;

    bool HasFilter() const;
    static FBlueprintGraphFilter FromPayload(const TSharedPtr<FJsonObject>& Payload);
};

struct FBlueprintGraphFilterResult
{
    TSet<UEdGraphNode*> IncludedNodes;
    int32 TotalNodes = 0;
    int32 MatchedNodes = 0;

    bool IncludesAll() const { return IncludedNodes.Num() == 0 && MatchedNodes == 0; }
    bool ShouldInclude(UEdGraphNode* Node) const { return IncludesAll() || IncludedNodes.Contains(Node); }
    TSharedPtr<FJsonObject> ToJson() const;
};

FBlueprintGraphFilterResult ApplyBlueprintGraphFilter(UEdGraph* Graph, const FBlueprintGraphFilter& Filter);
}
