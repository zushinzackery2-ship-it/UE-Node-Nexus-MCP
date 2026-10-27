#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/Creation/NexusBlueprintNodeDefaults.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "GraphEditorSettings.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusBlueprintDefaults, "Nexus.Issues2.BlueprintCreationDefaults", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusBlueprintDefaults::RunTest(const FString& Parameters)
{
    UGraphEditorSettings* Settings = GetMutableDefault<UGraphEditorSettings>();
    const FLinearColor PreviousColor = Settings->DefaultCommentNodeTitleColor;
    Settings->DefaultCommentNodeTitleColor = FLinearColor(0.2f, 0.4f, 0.6f, 0.8f);
    ON_SCOPE_EXIT
    {
        Settings->DefaultCommentNodeTitleColor = PreviousColor;
    };
    UEdGraph* Graph = NewObject<UEdGraph>();
    Graph->Schema = UEdGraphSchema_K2::StaticClass();
    FGraphNodeCreator<UEdGraphNode_Comment> Creator(*Graph);
    UEdGraphNode_Comment* Node = Creator.CreateNode();
    Creator.Finalize();
    TestEqual(TEXT("placement reads editor settings"), Node->CommentColor, Settings->DefaultCommentNodeTitleColor);
    UeNodeNexusBridge::InitializeBlueprintNodeDefaults(Node);
    const auto* Defaults = GetDefault<UEdGraphNode_Comment>();
    TestEqual(TEXT("MCP creation uses schema color"), Node->CommentColor, Defaults->CommentColor);
    TestEqual(TEXT("MCP creation uses schema bubble visibility"), bool(Node->bCommentBubbleVisible_InDetailsPanel), bool(Defaults->bCommentBubbleVisible_InDetailsPanel));
    TestEqual(TEXT("bubble UI follows its property"), bool(Node->bCommentBubbleVisible), bool(Node->bCommentBubbleVisible_InDetailsPanel));
    return true;
}

#endif
