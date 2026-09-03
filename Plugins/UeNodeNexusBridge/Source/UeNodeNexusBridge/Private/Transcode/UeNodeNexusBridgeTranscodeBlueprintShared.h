#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraphPin.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node_EditablePinBase;
struct FBPVariableDescription;

namespace UeNodeNexusBridge::Transcode
{
// FEdGraphPinType <-> raw dict {category, subcategory, subobject, container, is_ref, value_*}.
TSharedPtr<FJsonObject> PinTypeJson(const FEdGraphPinType& PinType);
bool PinTypeFromJson(const TSharedPtr<FJsonObject>& Json, FEdGraphPinType& OutType, FString& OutError);

// Variable description -> raw record (name, guid, type, default, category, tooltip, flags, rep_notify, metadata).
// Cdo (may be null) supplies the effective default once the compiler consumed DefaultValue.
TSharedPtr<FJsonObject> VariableJson(const FBPVariableDescription& Variable, UObject* Cdo = nullptr);
// Flags list (InstanceEditable, BlueprintReadOnly, ...) -> CPF property flags + metadata to set.
uint64 VariableFlagsFromList(const TArray<FString>& Flags, bool& bOutPrivate, bool& bOutMultiline);

// Graph export.
TSharedPtr<FJsonObject> GraphJson(UBlueprint* Blueprint, UEdGraph* Graph, const FString& Kind);
TSharedPtr<FJsonObject> NodeJson(UBlueprint* Blueprint, UEdGraphNode* Node, bool bForceOpaque);
// Class-specific configuration (function reference, variable name, macro, cast target, ...).
TSharedPtr<FJsonObject> NodeConfigJson(UBlueprint* Blueprint, UEdGraphNode* Node);
bool IsSupportedNodeClass(const UClass* Class);
// Signature record for a function graph (inputs/outputs/flags/category/locals).
TSharedPtr<FJsonObject> FunctionSignatureJson(UEdGraph* Graph);
// User-defined pins of an entry/result node -> [{name,type,default}].
TArray<TSharedPtr<FJsonValue>> UserPinsJson(UK2Node_EditablePinBase* Node);
// Pin default as text (object / text / string variants collapsed).
FString PinDefaultText(const UEdGraphPin* Pin);
}
