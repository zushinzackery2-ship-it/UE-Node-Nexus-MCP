#pragma once

#include "CoreMinimal.h"

class UMaterialExpressionMaterialFunctionCall;
class UMaterialFunctionInterface;

namespace UeNodeNexusBridge
{
// What pointing a call node at another function did to the links that read it.
struct FFunctionCallRebind
{
    // Inputs that now read the same-named output at its index in the new function.
    int32 Followed = 0;
    // "<reader>.<input> <- <output>" for links whose output name the new function lacks; these are cut.
    TArray<FString> Dropped;
};

// Text "None" or empty clears the call; otherwise an object path, with or without the
// export-text class prefix. False with OutError when the path names no material function.
bool ResolveCalledFunction(const FString& Text, UMaterialFunctionInterface*& OutFunction, FString& OutError);

// Points Call at Function (nullptr clears it) over the graph that owns Call. Inputs keep
// their links by input name, and every input of the owner graph that read a call output
// follows that output's name to its index in the new function.
//
// UE's own fixup runs only through the transient Material/Function back-pointers, which
// name a preview material while a function graph is edited (or nothing at all during a
// transcode apply), so it kept the old output index. Both back-pointers and the editor
// node are therefore held off during SetMaterialFunction, the owner graph is fixed up
// here instead, and the editor node is relinked afterwards. False with OutError when
// the call cannot use Function.
bool RebindFunctionCall(UMaterialExpressionMaterialFunctionCall* Call, UMaterialFunctionInterface* Function,
    FFunctionCallRebind& OutResult, FString& OutError);
}
