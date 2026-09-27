#include "NexusBlueprintNodeDefaults.h"

#include "EdGraphNode_Comment.h"

namespace UeNodeNexusBridge
{
void InitializeBlueprintNodeDefaults(UEdGraphNode* Node)
{
    if (auto* Comment = Cast<UEdGraphNode_Comment>(Node))
    {
        // PostPlacedNewNode uses per-user editor settings. MCP creation uses
        // the class defaults advertised by schema on every machine.
        const auto* Defaults = Comment->GetClass()->GetDefaultObject<UEdGraphNode_Comment>();
        Comment->MoveMode = Defaults->MoveMode;
        Comment->CommentColor = Defaults->CommentColor;
        Comment->bCommentBubbleVisible_InDetailsPanel = Defaults->bCommentBubbleVisible_InDetailsPanel;
        Comment->bCommentBubbleVisible = Defaults->bCommentBubbleVisible;
        Comment->bCommentBubblePinned = Defaults->bCommentBubblePinned;
        Comment->NodeComment = Defaults->NodeComment;
    }
}
}
