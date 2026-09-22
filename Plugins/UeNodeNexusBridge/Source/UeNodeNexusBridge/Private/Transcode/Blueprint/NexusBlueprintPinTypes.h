#pragma once

#include "CoreMinimal.h"

class UEdGraph;

namespace UeNodeNexusBridge::Transcode
{
struct FWildcardResolution
{
    int32 Before = 0;
    int32 Remaining = 0;
    // "<node title>.<pin name>" for every linked pin still without a type.
    TArray<FString> Unresolved;
};

// Settle wildcard pins after a batch of links has been applied. See the .cpp for
// why building a graph programmatically needs this and interactive editing does not.
FWildcardResolution ResolveWildcardPins(const TArray<UEdGraph*>& Graphs);
}
