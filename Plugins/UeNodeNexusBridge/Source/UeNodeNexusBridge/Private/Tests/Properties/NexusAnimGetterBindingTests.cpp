#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "Blueprint/Properties/NexusBlueprintNodeProperties.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_AnimGetter.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAnimGetterBindings,
    "Nexus.Issues7.Animation.GetterBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimGetterBindings::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    auto* Blueprint = NewObject<UAnimBlueprint>();
    auto* Graph = NewObject<UEdGraph>(Blueprint);
    auto* Getter = NewObject<UK2Node_AnimGetter>(Graph);
    auto* Player = NewObject<UAnimGraphNode_BlendSpacePlayer>(Graph);
    auto* ForeignBlueprint = NewObject<UAnimBlueprint>();
    auto* ForeignPlayer = NewObject<UAnimGraphNode_BlendSpacePlayer>(NewObject<UEdGraph>(ForeignBlueprint));
    FString Before, After, Error;
    auto Write = [&](const TCHAR* Name, UObject* Object, bool bDryRun)
    {
        return ApplyBlueprintNodeProperty(Getter, Name, MakeShared<FJsonValueString>(Object->GetPathName()),
            bDryRun, false, Before, After, Error);
    };
    TestTrue(TEXT("preview validates source"), Write(TEXT("SourceNode"), Player, true));
    TestNull(TEXT("preview retains original source"), Getter->SourceNode.Get());
    TestTrue(TEXT("source binds"), Write(TEXT("SourceNode"), Player, false));
    TestFalse(TEXT("foreign source rejected"), Write(TEXT("SourceNode"), ForeignPlayer, false));
    TestTrue(TEXT("rejection preserves source"), Getter->SourceNode == Player);
    TestFalse(TEXT("wrong source class rejected"), Write(TEXT("SourceNode"), Blueprint, false));
    TestTrue(TEXT("own blueprint binds"), Write(TEXT("SourceAnimBlueprint"), Blueprint, false));
    TestFalse(TEXT("foreign blueprint rejected"), Write(TEXT("SourceAnimBlueprint"), ForeignBlueprint, false));
    TestTrue(TEXT("AnimInstance class binds"), Write(TEXT("GetterClass"), UAnimInstance::StaticClass(), false));
    TestFalse(TEXT("wrong class rejected"), Write(TEXT("GetterClass"), UObject::StaticClass(), false));
    const auto Params = BuildBlueprintNodeParams(Getter);
    for (const FString Name : TArray<FString>
        {
            TEXT("SourceNode"), TEXT("SourceStateNode"), TEXT("GetterClass"),
            TEXT("SourceAnimBlueprint"), TEXT("CachedTitle"), TEXT("Contexts")
        })
    {
        TestTrue(TEXT("binding is readable: ") + Name, Params.ContainsByPredicate([&](const TSharedPtr<FJsonValue>& Value)
        {
            return Value->AsObject()->GetStringField(TEXT("name")) == Name;
        }));
    }
    return true;
}
#endif
