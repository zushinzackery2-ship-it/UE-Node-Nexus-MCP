#pragma once

#include "CoreMinimal.h"

class UClass;
class UEdGraphNode;
class UFunction;

namespace UeNodeNexusBridge
{
// The node classes whose instance is decided by the function they call: the
// editor's function spawner picks one of them from the UFUNCTION's metadata.
bool IsCallHostClass(const UClass* NodeClass);

// The class the editor itself spawns to call ``Function``. Wildcard array pins,
// data table row types and collection parameter names are resolved only by that
// class, so a plain call node for such a function can never compile.
UClass* CallHostClassFor(const UFunction* Function);

UFunction* FindCallFunction(const FString& OwnerPath, const FString& FunctionName);

// Replaces a requested call class by the one ``Function`` requires. The plain
// call class is a request for "whatever this function needs"; a specialised
// class that disagrees with the metadata is refused, never silently swapped.
// Without a resolvable function only the plain class is accepted unchanged.
bool SelectCallHostClass(UClass*& InOutClass, const UFunction* Function, FString& OutError);

// True when an existing call node sits on the class its function requires.
bool HasRequiredCallHost(const UEdGraphNode* Node);
}
