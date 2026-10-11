#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusComponentPropertyRebinding,
    "Nexus.Issues10.Level.ComponentPropertyRebinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusComponentPropertyRebinding::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusPropertyActor"));
    TStrongObjectPtr<UBlueprint> Blueprint(FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(),
        GetTransientPackage(), Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass()));
    auto* Node = Blueprint->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("Mesh"));
    Blueprint->SimpleConstructionScript->AddNode(Node);
    FKismetEditorUtilities::CompileBlueprint(Blueprint.Get());
    if (!TestTrue(TEXT("fixture Blueprint compiles"), Blueprint->Status != BS_Error))
    {
        return false;
    }
    const UWorld::InitializationValues Values = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World = UWorld::CreateWorld(EWorldType::Editor, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    ON_SCOPE_EXIT
    {
        World->DestroyWorld(false);
        World->RemoveFromRoot();
    };
    auto* Actor = World->SpawnActor<AActor>(Blueprint->GeneratedClass);
    auto* Original = Actor->FindComponentByClass<UStaticMeshComponent>();
    if (!TestTrue(TEXT("registered SCS component exists"), Original && Original->IsRegistered() && Original->IsCreatedByConstructionScript()))
    {
        return false;
    }
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Request->SetStringField(TEXT("component"), Original->GetName());
    Request->SetBoolField(TEXT("dry_run"), false);
    auto Properties = MakeShared<FJsonObject>();
    Properties->SetBoolField(TEXT("CastShadow"), false);
    Properties->SetBoolField(TEXT("LightingChannels.bChannel1"), true);
    Request->SetObjectField(TEXT("properties"), Properties);
    auto Response = UeNodeNexusBridge::HandleLevelActorPropertiesSet(TEXT("level_actor_properties_set"), TEXT("issues10-rebind"), Request);
    TestTrue(TEXT("batch reports success"), Response->GetBoolField(TEXT("ok")));
    auto* Current = Actor->FindComponentByClass<UStaticMeshComponent>();
    if (TestNotNull(TEXT("construction leaves a current component"), Current))
    {
        TestTrue(TEXT("fixture exercised actual component replacement"), Current != Original);
        TestFalse(TEXT("scalar reached the live component"), Current->CastShadow != 0);
        TestTrue(TEXT("nested container reached the live component"), Current->LightingChannels.bChannel1 != 0);
    }
    Properties->SetBoolField(TEXT("UnknownNexusProperty"), true);
    Properties->SetBoolField(TEXT("CastShadow"), true);
    Response = UeNodeNexusBridge::HandleLevelActorPropertiesSet(TEXT("level_actor_properties_set"), TEXT("issues10-invalid"), Request);
    TestFalse(TEXT("invalid batch fails during preflight"), Response->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("preflight failure writes no valid property"), Actor->FindComponentByClass<UStaticMeshComponent>()->CastShadow != 0);
    return true;
}
#endif
