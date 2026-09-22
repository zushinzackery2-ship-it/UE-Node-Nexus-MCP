#include "NexusBlueprintPinNames.h"

#include "EdGraph/EdGraphNode.h"
#include "K2Node_DynamicCast.h"

namespace UeNodeNexusBridge::Transcode
{
const TCHAR* const CastResultPinAlias = TEXT("AsResult");

FString MirrorPinName(const UEdGraphPin* Pin)
{
    if (Pin == nullptr)
    {
        return FString();
    }
    const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Pin->GetOwningNodeUnchecked());
    if (CastNode != nullptr && CastNode->GetCastResultPin() == Pin)
    {
        return CastResultPinAlias;
    }
    return Pin->PinName.ToString();
}

UEdGraphPin* FindAliasedPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    if (Node == nullptr || Direction != EGPD_Output || !PinName.Equals(CastResultPinAlias, ESearchCase::IgnoreCase))
    {
        return nullptr;
    }
    const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node);
    return CastNode != nullptr ? CastNode->GetCastResultPin() : nullptr;
}
}
