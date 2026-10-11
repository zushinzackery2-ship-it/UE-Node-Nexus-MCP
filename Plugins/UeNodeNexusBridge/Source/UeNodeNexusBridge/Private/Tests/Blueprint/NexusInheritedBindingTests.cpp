#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SphereComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

using namespace UeNodeNexusBridge::Transcode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInheritedComponentBinding,
    "Nexus.Issues7.Blueprint.InheritedComponentBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInheritedComponentBinding::RunTest(const FString& Parameters)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusSpectator"));
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(ASpectatorPawn::StaticClass(), GetTransientPackage(),
        Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    auto Binding = MakeShared<FJsonObject>();
    Binding->SetStringField(TEXT("name"), TEXT("CollisionComponent"));
    Binding->SetStringField(TEXT("class"), TEXT("/Script/Engine.SphereComponent"));
    FString Error;
    TestTrue(TEXT("native parent CDO identity binds"), ApplyComponentVerb(Blueprint, TEXT("bp_component_bind"), Binding, Error));
    auto Child = MakeShared<FJsonObject>();
    Child->SetStringField(TEXT("name"), TEXT("View"));
    Child->SetStringField(TEXT("class"), TEXT("/Script/Engine.CameraComponent"));
    Child->SetStringField(TEXT("parent"), TEXT("CollisionComponent"));
    if (!TestTrue(TEXT("new Camera attaches to the inherited root"), ApplyComponentVerb(Blueprint, TEXT("bp_component_add"), Child, Error)))
    {
        AddError(Error);
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TestTrue(TEXT("attached component Blueprint compiles"), Blueprint->Status != BS_Error);
    const auto* Node = Blueprint->SimpleConstructionScript->FindSCSNode(TEXT("View"));
    TestTrue(TEXT("attachment retains native parent identity"), Node && Node->bIsParentComponentNative
        && Node->ParentComponentOrVariableName == ADefaultPawn::CollisionComponentName);
    Binding->SetStringField(TEXT("class"), TEXT("/Script/Engine.CameraComponent"));
    TestFalse(TEXT("wrong inherited class is rejected"), ApplyComponentVerb(Blueprint, TEXT("bp_component_bind"), Binding, Error));
    Binding->SetStringField(TEXT("name"), TEXT("InventedComponent"));
    TestFalse(TEXT("missing inherited identity is rejected"), ApplyComponentVerb(Blueprint, TEXT("bp_component_bind"), Binding, Error));
    return true;
}
#endif
