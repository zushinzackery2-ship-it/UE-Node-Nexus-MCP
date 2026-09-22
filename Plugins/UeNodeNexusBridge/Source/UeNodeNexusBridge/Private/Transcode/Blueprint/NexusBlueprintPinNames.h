#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

class UEdGraphNode;

namespace UeNodeNexusBridge::Transcode
{
// The cast result pin is named "As" + TargetType->GetDisplayNameText() by the
// engine. That text is localized, so the real pin name differs between editor
// languages and a mirror written on one editor cannot be applied on another.
// The mirror writes this alias instead and resolves it through the node.
extern const TCHAR* const CastResultPinAlias;

// The pin name the mirror writes. Identical to PinName for every pin whose name
// the engine does not build out of localized text.
FString MirrorPinName(const UEdGraphPin* Pin);

// The pin a mirror name refers to when that name is an alias rather than a real
// pin name; nullptr when the name is not an alias.
UEdGraphPin* FindAliasedPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);
}
