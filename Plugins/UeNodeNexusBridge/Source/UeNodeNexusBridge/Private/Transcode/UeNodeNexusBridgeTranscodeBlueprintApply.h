#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

namespace UeNodeNexusBridge::Transcode
{
struct FApplyContext;

FString ReadOpString(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field, const FString& Default = FString());
UEdGraphNode* ResolveGraphNode(UBlueprint* Blueprint, UEdGraph* Graph, const FApplyContext& Context, const FString& LocalId);
UClass* ResolveClassByNameOrPath(const FString& Text);
// Empty pin name resolves to the single visible data pin in that direction.
UEdGraphPin* ResolvePin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);
// ``pin:X`` / ``prop:X`` / plain name (pin first, then editable property); null value resets.
bool SetNodeParam(UBlueprint* Blueprint, UEdGraph* Graph, UEdGraphNode* Node, const FString& RawName, const TSharedPtr<FJsonValue>& Value, FString& OutError);
bool SetDynamicPins(UEdGraphNode* Node, int32 Count, FString& OutError);
// Positional text args ("Owner.Func", "Var", ...) -> config fields understood by the node creators.
bool ConfigureFromPositional(UBlueprint* Blueprint, UClass* NodeClass, const TArray<FString>& Positional, const TSharedPtr<FJsonObject>& Config, FString& OutError);

// Graph node verbs (create/delete/param/position/links) for one graph.
void ApplyBlueprintGraphVerb(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context);
void ApplyBlueprintCreateNode(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context);
// Member verbs (variables, components, defaults, functions, locals); returns false when the verb is not a member verb.
bool ApplyBlueprintMemberVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context);
// Member verb families (each returns false for verbs it does not own).
bool ApplyVariableVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError);
bool ApplyComponentVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError);
bool ApplyFunctionVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError);
bool ApplyLocalVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError);
// Shared payload readers.
TArray<FString> ReadOpStrings(const TSharedPtr<FJsonObject>& Op, const TCHAR* Field);
bool ReadOpPinType(const TSharedPtr<FJsonObject>& Op, FEdGraphPinType& OutType, FString& OutError);
}
