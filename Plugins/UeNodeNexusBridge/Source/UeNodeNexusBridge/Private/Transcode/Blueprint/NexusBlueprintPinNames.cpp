#include "NexusBlueprintPinNames.h"

#include "EdGraph/EdGraphNode.h"
#include "K2Node_DynamicCast.h"

namespace UeNodeNexusBridge::Transcode
{
const TCHAR* const CastResultPinAlias = TEXT("AsResult");

namespace
{
// Pin names the engine generates or shortens that authors should not have to
// intone. Array Get Item builds ``Array`` / ``Dimension 1`` / ``Output``; the
// Kismet array spelling authors know from Array_Get and siblings is
// ``TargetArray`` / ``Index`` / ``Item``, which the mirror therefore accepts.
const FPinAlias GeneratedPinAliases[] =
{
    { TEXT("K2Node_GetArrayItem"), TEXT("TargetArray"), TEXT("Array"), EGPD_Input },
    { TEXT("K2Node_GetArrayItem"), TEXT("Index"), TEXT("Dimension 1"), EGPD_Input },
    { TEXT("K2Node_GetArrayItem"), TEXT("Item"), TEXT("Output"), EGPD_Output },
};
}

TArray<FPinAlias> PinAliasesForClass(const UClass* NodeClass)
{
    TArray<FPinAlias> Result;
    if (NodeClass == nullptr)
    {
        return Result;
    }
    const FString Name = NodeClass->GetName();
    for (const FPinAlias& Alias : GeneratedPinAliases)
    {
        if (Name == Alias.OwnerClass)
        {
            Result.Add(Alias);
        }
    }
    return Result;
}

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
    if (Node == nullptr)
    {
        return nullptr;
    }
    if (Direction == EGPD_Output && PinName.Equals(CastResultPinAlias, ESearchCase::IgnoreCase))
    {
        const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node);
        return CastNode != nullptr ? CastNode->GetCastResultPin() : nullptr;
    }
    for (const FPinAlias& Alias : PinAliasesForClass(Node->GetClass()))
    {
        if (Alias.Direction != Direction || !PinName.Equals(Alias.Alias, ESearchCase::IgnoreCase))
        {
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr && Pin->Direction == Direction
                && Pin->PinName.ToString().Equals(Alias.PinName, ESearchCase::IgnoreCase))
            {
                return Pin;
            }
        }
    }
    return nullptr;
}

FString DescribePinNames(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
    if (Node == nullptr)
    {
        return FString();
    }
    TArray<FString> Names;
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && Pin->Direction == Direction && !Pin->bHidden)
        {
            Names.Add(Pin->PinName.ToString());
        }
    }
    FString Result = FString::Printf(TEXT("%s pins: %s"),
        Direction == EGPD_Input ? TEXT("in") : TEXT("out"), *FString::Join(Names, TEXT(", ")));
    TArray<FString> Aliases;
    for (const FPinAlias& Alias : PinAliasesForClass(Node->GetClass()))
    {
        if (Alias.Direction == Direction)
        {
            Aliases.Add(FString::Printf(TEXT("%s->%s"), Alias.Alias, Alias.PinName));
        }
    }
    if (Aliases.Num() > 0)
    {
        Result += FString::Printf(TEXT("; aliases: %s"), *FString::Join(Aliases, TEXT(", ")));
    }
    return Result;
}
}
