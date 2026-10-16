#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UEdGraphNode;
class UClass;

namespace UeNodeNexusBridge::Transcode
{
// The cast result pin is named "As" + TargetType->GetDisplayNameText() by the
// engine. That text is localized, so the real pin name differs between editor
// languages and a mirror written on one editor cannot be applied on another.
// The mirror writes this alias instead and resolves it through the node.
extern const TCHAR* const CastResultPinAlias;

// One author-facing spelling the mirror accepts for a pin whose real name the
// engine generates or shortens. The canonical export keeps the real name; the
// alias only resolves what authors and older samples wrote.
struct FPinAlias
{
    const TCHAR* OwnerClass;
    const TCHAR* Alias;
    const TCHAR* PinName;
    EEdGraphPinDirection Direction;
};

// The aliases the node class answers to; empty for classes without any.
TArray<FPinAlias> PinAliasesForClass(const UClass* NodeClass);

// The pin name the mirror writes. Identical to PinName for every pin whose name
// the engine does not build out of localized text.
FString MirrorPinName(const UEdGraphPin* Pin);

// The pin a mirror name refers to: a real pin name, the cast alias, or a
// registered author alias; nullptr when nothing matches.
UEdGraphPin* FindAliasedPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);

// Human-readable pin inventory of one direction, aliases included, for
// pin_not_found diagnostics.
FString DescribePinNames(UEdGraphNode* Node, EEdGraphPinDirection Direction);
}
