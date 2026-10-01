#include "NexusVariableTypeLinks.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_GetArrayItem.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Select.h"
#include "Blueprint/NexusBlueprintPinTypes.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Transcode
{
static bool IsTypeDependent(UK2Node* Node)
{
    return Node && (Node->IsA<UK2Node_MacroInstance>() || Node->IsA<UK2Node_CallArrayFunction>()
        || Node->IsA<UK2Node_Knot>() || Node->IsA<UK2Node_GetArrayItem>()
        || Node->IsA<UK2Node_MakeArray>() || Node->IsA<UK2Node_Select>());
}

FVariableTypeLinks::FVariableTypeLinks(const TArray<UK2Node*>& Variables)
{
    TArray<UK2Node*> Pending = Variables;
    TSet<UK2Node*> Visited(Variables);
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        UK2Node* Node = Pending[Index];
        Graphs.AddUnique(Node->GetGraph());
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                auto* Next = Cast<UK2Node>(Linked->GetOwningNode());
                if (IsTypeDependent(Next) && !Visited.Contains(Next))
                {
                    Visited.Add(Next);
                    Pending.Add(Next);
                    Dependents.Add(Next);
                }
            }
        }
    }
    // Record each edge once, including edges from a fixed-type boundary node.
    TSet<TPair<const UEdGraphPin*, const UEdGraphPin*>> Seen;
    for (UK2Node* Node : Pending)
    {
        Node->Modify();
        for (UEdGraphPin* Pin : Node->Pins)
        {
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                auto* Output = Pin->Direction == EGPD_Output ? Pin : Linked;
                auto* Input = Pin->Direction == EGPD_Input ? Pin : Linked;
                const TPair<const UEdGraphPin*, const UEdGraphPin*> Pair(Output, Input);
                if (Seen.Contains(Pair))
                {
                    continue;
                }
                Seen.Add(Pair);
                FLink Link;
                Link.Output = FEndpoint
                {
                    Output->GetOwningNode(), Output->PinName, EGPD_Output
                };
                Link.Input = FEndpoint
                {
                    Input->GetOwningNode(), Input->PinName, EGPD_Input
                };
                Links.Add(Link);
            }
        }
    }
    // Detach the whole component before any notifications can infer from an old
    // neighbour. Pin names survive reconstruction; pin pointers do not.
    for (UK2Node* Node : Pending)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            Pin->Modify();
            Pin->BreakAllPinLinks(false);
        }
    }
    for (UK2Node* Node : Dependents)
    {
        if (auto* Macro = Cast<UK2Node_MacroInstance>(Node))
        {
            Macro->ResolvedWildcardType.ResetToDefaults();
        }
        for (int32 Index = 0; Index < Node->Pins.Num(); ++Index)
        {
            Node->PinConnectionListChanged(Node->Pins[Index]);
        }
        Node->ReconstructNode();
    }
}

bool FVariableTypeLinks::Restore(FString& OutError)
{
    bool bComplete = true;
    for (const FLink& Link : Links)
    {
        auto* Output = Link.Output.Node->FindPin(Link.Output.Name, Link.Output.Direction);
        auto* Input = Link.Input.Node->FindPin(Link.Input.Name, Link.Input.Direction);
        if (Output && Input)
        {
            // Reattach existing intent without schema autocasts or dropping
            // incompatible links. Candidate edits run next, then compile checks.
            Output->MakeLinkTo(Input);
        }
        else
        {
            bComplete = false;
            OutError += FString::Printf(TEXT("Type change removed a connected pin: %s.%s -> %s.%s; "),
                *Link.Output.Node->NodeGuid.ToString(), *Link.Output.Name.ToString(),
                *Link.Input.Node->NodeGuid.ToString(), *Link.Input.Name.ToString());
        }
    }
    const FWildcardResolution Result = ResolveWildcardPins(Graphs);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=variable_type_links nodes=%d links=%d unresolved=%d"),
        *ActiveBridgeRequestId(), Dependents.Num(), Links.Num(), Result.Remaining);
    return bComplete;
}
}
