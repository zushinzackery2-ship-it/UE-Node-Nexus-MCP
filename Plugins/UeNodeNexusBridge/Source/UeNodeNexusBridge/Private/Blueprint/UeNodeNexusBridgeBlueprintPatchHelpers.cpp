#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
UEdGraph* FindBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName)
{
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph != nullptr && (GraphName.IsEmpty() || Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase)))
        {
            return Graph;
        }
    }
    return nullptr;
}

UEdGraphNode* FindBlueprintNode(UEdGraph* Graph, const FString& NodeId)
{
    FGuid Guid;
    if (Graph == nullptr || !FGuid::Parse(NodeId, Guid))
    {
        return nullptr;
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node != nullptr && Node->NodeGuid == Guid)
        {
            return Node;
        }
    }
    return nullptr;
}

UEdGraphPin* FindBlueprintPin(UEdGraphNode* Node, const FString& PinId)
{
    FGuid Guid;
    if (Node == nullptr || !FGuid::Parse(PinId, Guid))
    {
        return nullptr;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin != nullptr && Pin->PinId == Guid)
        {
            return Pin;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> MakeBlueprintLinkJson(UEdGraphPin* FromPin, UEdGraphPin* ToPin)
{
    TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
    Link->SetStringField(TEXT("from_node_id"), FromPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Link->SetStringField(TEXT("from_pin_id"), FromPin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    Link->SetStringField(TEXT("to_node_id"), ToPin->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Link->SetStringField(TEXT("to_pin_id"), ToPin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
    return Link;
}

bool ResolveBlueprintLinkPins(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, UEdGraphPin*& OutFrom, UEdGraphPin*& OutTo)
{
    FString FromNodeId;
    FString FromPinId;
    FString ToNodeId;
    FString ToPinId;
    if (!Op->TryGetStringField(TEXT("from_node_id"), FromNodeId) || !Op->TryGetStringField(TEXT("from_pin_id"), FromPinId) || !Op->TryGetStringField(TEXT("to_node_id"), ToNodeId) || !Op->TryGetStringField(TEXT("to_pin_id"), ToPinId))
    {
        return false;
    }

    UEdGraphNode* FromNode = FindBlueprintNode(Graph, FromNodeId);
    UEdGraphNode* ToNode = FindBlueprintNode(Graph, ToNodeId);
    OutFrom = FindBlueprintPin(FromNode, FromPinId);
    OutTo = FindBlueprintPin(ToNode, ToPinId);
    return OutFrom != nullptr && OutTo != nullptr;
}

TSharedPtr<FJsonObject> BuildBlueprintPinIntegrity(UEdGraph* Graph)
{
    TArray<TSharedPtr<FJsonValue>> Broken;
    TArray<TSharedPtr<FJsonValue>> Missing;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr)
            {
                continue;
            }
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                if (Linked == nullptr || Linked->GetOwningNode() == nullptr)
                {
                    Missing.Add(MakeShared<FJsonValueString>(Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens)));
                }
                else if (!Linked->LinkedTo.Contains(Pin))
                {
                    Broken.Add(MakeShared<FJsonValueObject>(MakeBlueprintLinkJson(Pin, Linked)));
                }
            }
        }
    }
    return MakePinIntegrity(Broken.Num() == 0 && Missing.Num() == 0, Broken, Missing);
}

TArray<TSharedPtr<FJsonValue>> CompileBlueprintWithDiagnostics(UBlueprint* Blueprint, const FString& AssetPath, TSharedPtr<FJsonObject>& OutCompile)
{
    FCompilerResultsLog Results;
    Results.bSilentMode = true;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
    const bool bOk = Results.NumErrors == 0 && Blueprint->Status != BS_Error;
    OutCompile = MakeCompilePostCheck(true, true, bOk, Results.NumErrors, Results.NumWarnings);

    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
    {
        const FString Severity = Message->GetSeverity() == EMessageSeverity::Error ? TEXT("error") : TEXT("warning");
        Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(Severity, TEXT("compile_message"), Message->ToText().ToString(), AssetPath, TEXT("Unreal"))));
    }
    return Diagnostics;
}
}
