#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace UeNodeNexusBridge::Transcode
{
static void ApplyLink(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context, bool bConnect)
{
    const FString FromId = ReadOpString(Op, TEXT("from"));
    const FString ToId = ReadOpString(Op, TEXT("to"));
    UEdGraphNode* From = ResolveGraphNode(Blueprint, Graph, Context, FromId);
    UEdGraphNode* To = ResolveGraphNode(Blueprint, Graph, Context, ToId);
    if (Context.bDryRun)
    {
        return;
    }
    if (From == nullptr || To == nullptr)
    {
        Context.Fail(Index, TEXT("node_not_found"), FString::Printf(TEXT("link endpoint not found: %s -> %s"), *FromId, *ToId));
        return;
    }
    UEdGraphPin* FromPin = ResolvePin(From, ReadOpString(Op, TEXT("from_pin")), EGPD_Output);
    UEdGraphPin* ToPin = ResolvePin(To, ReadOpString(Op, TEXT("to_pin")), EGPD_Input);
    if (FromPin == nullptr || ToPin == nullptr)
    {
        Context.Fail(Index, TEXT("pin_not_found"), FString::Printf(TEXT("pin not found: %s.%s -> %s.%s"), *FromId, *ReadOpString(Op, TEXT("from_pin")), *ToId, *ReadOpString(Op, TEXT("to_pin"))));
        return;
    }
    Graph->Modify();
    From->Modify();
    To->Modify();
    if (bConnect)
    {
        if (!Graph->GetSchema()->TryCreateConnection(FromPin, ToPin))
        {
            Context.Fail(Index, TEXT("connect_failed"), FString::Printf(TEXT("schema refused %s.%s -> %s.%s"), *FromId, *FromPin->PinName.ToString(), *ToId, *ToPin->PinName.ToString()));
            return;
        }
    }
    else
    {
        FromPin->BreakLinkTo(ToPin);
    }
    Context.bChanged = true;
}

void ApplyBlueprintGraphVerb(UBlueprint* Blueprint, UEdGraph* Graph, const TSharedPtr<FJsonObject>& Op, int32 Index, FApplyContext& Context)
{
    const FString Verb = ReadOpString(Op, TEXT("op"));
    if (Verb == TEXT("create_node"))
    {
        ApplyBlueprintCreateNode(Blueprint, Graph, Op, Index, Context);
        return;
    }
    if (Verb == TEXT("connect_pins") || Verb == TEXT("disconnect_pins"))
    {
        ApplyLink(Blueprint, Graph, Op, Index, Context, Verb == TEXT("connect_pins"));
        return;
    }
    const FString Id = ReadOpString(Op, TEXT("id"));
    UEdGraphNode* Node = ResolveGraphNode(Blueprint, Graph, Context, Id);
    if (Node == nullptr)
    {
        if (!Context.bDryRun || !Context.Ids.Contains(Id))
        {
            Context.Fail(Index, TEXT("node_not_found"), FString::Printf(TEXT("node not found: %s"), *Id));
        }
        return;
    }
    if (Context.bDryRun)
    {
        return;
    }
    FString Error;
    if (Verb == TEXT("delete_node"))
    {
        FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    }
    else if (Verb == TEXT("set_node_param"))
    {
        if (!SetNodeParam(Blueprint, Graph, Node, ReadOpString(Op, TEXT("name")), Op->TryGetField(TEXT("value")), Error))
        {
            Context.Fail(Index, TEXT("param_failed"), FString::Printf(TEXT("%s: %s"), *Id, *Error));
            return;
        }
    }
    else if (Verb == TEXT("set_node_pins"))
    {
        int32 Count = 0;
        Op->TryGetNumberField(TEXT("count"), Count);
        if (!SetDynamicPins(Node, Count, Error))
        {
            Context.Fail(Index, TEXT("pins_failed"), Error);
            return;
        }
    }
    else if (Verb == TEXT("set_node_position"))
    {
        int32 X = 0;
        int32 Y = 0;
        Op->TryGetNumberField(TEXT("x"), X);
        Op->TryGetNumberField(TEXT("y"), Y);
        Node->Modify();
        Node->NodePosX = X;
        Node->NodePosY = Y;
    }
    else if (Verb == TEXT("set_node_enabled"))
    {
        bool bEnabled = true;
        Op->TryGetBoolField(TEXT("enabled"), bEnabled);
        Node->Modify();
        Node->SetEnabledState(bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled);
    }
    else if (Verb == TEXT("set_node_comment"))
    {
        Node->Modify();
        Node->NodeComment = ReadOpString(Op, TEXT("text"));
        Node->bCommentBubbleVisible = !Node->NodeComment.IsEmpty();
    }
    else
    {
        Context.Fail(Index, TEXT("unsupported_verb"), FString::Printf(TEXT("%s is not supported on Blueprint graphs"), *Verb));
        return;
    }
    Context.bChanged = true;
}
}
