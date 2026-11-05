#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/HUD.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Transcode/Blueprint/Apply/UeNodeNexusBridgeTranscodeBlueprintApply.h"

using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusExplicitDefaultSceneRoot,
    "Nexus.Issues26.Blueprint.ExplicitDefaultSceneRoot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusExplicitDefaultSceneRoot::RunTest(const FString& Parameters)
{
    for (UClass* Parent : {AActor::StaticClass(), AHUD::StaticClass()})
    {
        const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusExplicitRoot"));
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent,
            GetTransientPackage(), Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        USimpleConstructionScript* Scs = Blueprint->SimpleConstructionScript;
        USCS_Node* OriginalRoot = Scs->GetDefaultSceneRootNode();
        auto Op = MakeShared<FJsonObject>();
        Op->SetStringField(TEXT("name"), TEXT("DefaultSceneRoot"));
        Op->SetStringField(TEXT("class"), TEXT("/Script/Engine.SceneComponent"));
        FString Error;
        TestTrue(TEXT("explicit default root is accepted"), ApplyComponentVerb(Blueprint, TEXT("bp_component_add"), Op, Error));
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        auto* ExactRoot = Scs->FindSCSNode(TEXT("DefaultSceneRoot"));
        TestNotNull(TEXT("authored root name survives compile"), ExactRoot);
        TestEqual(TEXT("engine default root is reused"), ExactRoot, OriginalRoot);
        TestEqual(TEXT("one component in the construction script"), Scs->GetAllNodes().Num(), 1);
        if (ExactRoot)
        {
            TestEqual(TEXT("root retains the requested class"), ExactRoot->ComponentClass.Get(), USceneComponent::StaticClass());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusComponentNameCollision,
    "Nexus.Issues26.Blueprint.ComponentNameCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusComponentNameCollision::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusComponentCollision"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    auto Op = MakeShared<FJsonObject>();
    Op->SetStringField(TEXT("name"), TEXT("Root"));
    Op->SetStringField(TEXT("class"), TEXT("/Script/Engine.SceneComponent"));
    FString Error;
    TestTrue(TEXT("new explicit root is created"), ApplyComponentVerb(Blueprint, TEXT("bp_component_add"), Op, Error));
    const int32 Before = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
    TestFalse(TEXT("a colliding authored name is rejected"), ApplyComponentVerb(Blueprint, TEXT("bp_component_add"), Op, Error));
    TestEqual(TEXT("rejected addition does not create a renamed component"), Blueprint->SimpleConstructionScript->GetAllNodes().Num(), Before);
    TestTrue(TEXT("collision has an actionable diagnostic"), Error.Contains(TEXT("name")));
    Op->SetStringField(TEXT("name"), TEXT("Missing"));
    TestFalse(TEXT("a genuinely missing component still fails"), ApplyComponentVerb(Blueprint, TEXT("bp_component_set_prop"), Op, Error));
    return true;
}

#endif
