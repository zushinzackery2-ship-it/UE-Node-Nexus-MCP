#include "UeNodeNexusBridgeBlueprintPatchResolve.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "NodeInterface/UeNodeNexusBridgeBlueprintNodeInterfaceOps.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
FString ReadBlueprintPatchNodeRef(
    const TSharedPtr<FJsonObject>& Operation,
    const TCHAR* CanonicalField,
    const TCHAR* AliasField)
{
    FString Value;
    if (Operation->TryGetStringField(CanonicalField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    if (Operation->TryGetStringField(AliasField, Value) && !Value.IsEmpty())
    {
        return Value;
    }
    return FString();
}

UEdGraphNode* ResolveBlueprintPatchNode(
    UEdGraph* Graph,
    const FString& NodeId,
    const FBlueprintPatchContext& Context)
{
    if (UEdGraphNode* const* Found = Context.ClientNodes.Find(NodeId))
    {
        return *Found;
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("node_id"), NodeId);
    return ResolveBlueprintNodeInterfaceNode(Graph, Payload);
}

UEdGraphPin* ResolveBlueprintPatchPinByIdOrName(
    UEdGraphNode* Node,
    const TSharedPtr<FJsonObject>& Operation,
    const TCHAR* IdField,
    const TCHAR* NameField,
    EEdGraphPinDirection Direction)
{
    if (Node == nullptr)
    {
        return nullptr;
    }

    FString PinId;
    if (Operation->TryGetStringField(IdField, PinId))
    {
        return FindBlueprintPin(Node, PinId);
    }

    FString PinName;
    if (!Operation->TryGetStringField(NameField, PinName))
    {
        return nullptr;
    }

    if (UEdGraphPin* ByName = Node->FindPin(FName(*PinName), Direction))
    {
        return ByName;
    }
    if (UEdGraphPin* ByAnyDirection = Node->FindPin(FName(*PinName)))
    {
        return ByAnyDirection;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr)
        {
            continue;
        }
        const FString Friendly =
            Pin->PinFriendlyName.IsEmpty()
            ? Pin->PinName.ToString()
            : Pin->PinFriendlyName.ToString();
        if (Friendly.Equals(PinName, ESearchCase::IgnoreCase)
            && (Pin->Direction == Direction || Direction == EGPD_MAX))
        {
            return Pin;
        }
    }
    return nullptr;
}

static TSharedPtr<FJsonValueObject> MakePatchDiagnostic(
    const FString& Code,
    const FString& Message,
    UBlueprint* Blueprint)
{
    return MakeShared<FJsonValueObject>(
        MakeDiagnostic(
            TEXT("error"),
            Code,
            Message,
            Blueprint->GetPathName(),
            TEXT("UeNodeNexusBridge")));
}

bool ResolveBlueprintPatchLinkPins(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Operation,
    const FBlueprintPatchContext& Context,
    UEdGraphPin*& OutFrom,
    UEdGraphPin*& OutTo,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    UBlueprint* Blueprint)
{
    const FString FromNodeId =
        ReadBlueprintPatchNodeRef(Operation, TEXT("from_node_id"), TEXT("from_node"));
    const FString ToNodeId =
        ReadBlueprintPatchNodeRef(Operation, TEXT("to_node_id"), TEXT("to_node"));
    FString FromPinRef;
    FString ToPinRef;
    Operation->TryGetStringField(TEXT("from_pin_id"), FromPinRef)
        || Operation->TryGetStringField(TEXT("from_pin"), FromPinRef);
    Operation->TryGetStringField(TEXT("to_pin_id"), ToPinRef)
        || Operation->TryGetStringField(TEXT("to_pin"), ToPinRef);

    if (FromNodeId.IsEmpty() || ToNodeId.IsEmpty())
    {
        Diagnostics.Add(
            MakePatchDiagnostic(
                TEXT("missing_node_ref"),
                TEXT("connect_pins requires from_node_id and to_node_id"),
                Blueprint));
        return false;
    }

    UEdGraphNode* FromNode = ResolveBlueprintPatchNode(Graph, FromNodeId, Context);
    if (FromNode == nullptr)
    {
        Diagnostics.Add(
            MakePatchDiagnostic(
                TEXT("source_node_not_found"),
                FString::Printf(TEXT("Source node not found: %s"), *FromNodeId),
                Blueprint));
        return false;
    }

    UEdGraphNode* ToNode = ResolveBlueprintPatchNode(Graph, ToNodeId, Context);
    if (ToNode == nullptr)
    {
        Diagnostics.Add(
            MakePatchDiagnostic(
                TEXT("target_node_not_found"),
                FString::Printf(TEXT("Target node not found: %s"), *ToNodeId),
                Blueprint));
        return false;
    }

    OutFrom = ResolveBlueprintPatchPinByIdOrName(
        FromNode,
        Operation,
        TEXT("from_pin_id"),
        TEXT("from_pin"),
        EGPD_Output);
    if (OutFrom == nullptr)
    {
        TArray<FString> AvailableOutputs;
        for (UEdGraphPin* Pin : FromNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Output)
            {
                AvailableOutputs.Add(Pin->PinName.ToString());
            }
        }
        Diagnostics.Add(
            MakePatchDiagnostic(
                TEXT("source_pin_not_found"),
                FString::Printf(
                    TEXT("Source pin '%s' not found on node %s. Available outputs: [%s]"),
                    *FromPinRef,
                    *FromNodeId,
                    *FString::Join(AvailableOutputs, TEXT(", "))),
                Blueprint));
        return false;
    }

    OutTo = ResolveBlueprintPatchPinByIdOrName(
        ToNode,
        Operation,
        TEXT("to_pin_id"),
        TEXT("to_pin"),
        EGPD_Input);
    if (OutTo == nullptr)
    {
        TArray<FString> AvailableInputs;
        for (UEdGraphPin* Pin : ToNode->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input)
            {
                AvailableInputs.Add(Pin->PinName.ToString());
            }
        }
        Diagnostics.Add(
            MakePatchDiagnostic(
                TEXT("target_pin_not_found"),
                FString::Printf(
                    TEXT("Target pin '%s' not found on node %s. Available inputs: [%s]"),
                    *ToPinRef,
                    *ToNodeId,
                    *FString::Join(AvailableInputs, TEXT(", "))),
                Blueprint));
        return false;
    }

    return true;
}

UEdGraphNode* ResolveBlueprintPatchOpNode(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Operation,
    const FBlueprintPatchContext& Context,
    FString& OutNodeId)
{
    OutNodeId = ReadBlueprintPatchNodeRef(Operation, TEXT("node_id"), TEXT("node"));
    return OutNodeId.IsEmpty()
        ? nullptr
        : ResolveBlueprintPatchNode(Graph, OutNodeId, Context);
}
}
