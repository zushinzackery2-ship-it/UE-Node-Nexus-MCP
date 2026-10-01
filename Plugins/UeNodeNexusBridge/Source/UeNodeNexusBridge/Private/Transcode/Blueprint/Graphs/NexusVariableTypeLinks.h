#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UK2Node;
class UEdGraph;

namespace UeNodeNexusBridge::Transcode
{
// Preserve authored edges across a type edit. The final candidate's graph verbs
// and compiler still decide which connections are valid for the new type.
class FVariableTypeLinks
{
public:
    explicit FVariableTypeLinks(const TArray<UK2Node*>& Variables);
    bool Restore(FString& OutError);

private:
    struct FEndpoint
    {
        UEdGraphNode* Node = nullptr;
        FName Name;
        EEdGraphPinDirection Direction = EGPD_Input;
    };
    struct FLink
    {
        FEndpoint Output;
        FEndpoint Input;
    };
    TArray<UK2Node*> Dependents;
    TArray<UEdGraph*> Graphs;
    TArray<FLink> Links;
};
}
