#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/CapsuleComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Character.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Transcode/Blueprint/Components/NexusNativeComponentTemplates.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateConfig.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNativeCharacterComponent,
    "Nexus.Issues3.Character.NativeComponentDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNativeCharacterComponent::RunTest(const FString& Parameters)
{
    const auto* Parent = GetDefault<ACharacter>();
    const float ParentRadius = Parent->GetCapsuleComponent()->GetUnscaledCapsuleRadius();
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusCharacterTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ACharacter::StaticClass(),
        GetTransientPackage(), Name, BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    auto Op = MakeShared<FJsonObject>();
    Op->SetStringField(TEXT("name"), TEXT("CollisionCylinder"));
    Op->SetStringField(TEXT("prop"), TEXT("CapsuleRadius"));
    Op->SetStringField(TEXT("value"), TEXT("31"));
    FString Error;
    TestTrue(TEXT("native child component property is writable"),
        Transcode::ApplyComponentVerb(Blueprint, TEXT("bp_component_set_prop"), Op, Error));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const auto* Child = Blueprint->GeneratedClass->GetDefaultObject<ACharacter>();
    TestEqual(TEXT("override survives compilation"), Child->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), 31.0f);
    TestEqual(TEXT("parent template remains unchanged"), Parent->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), ParentRadius);
    TestFalse(TEXT("native components cannot be removed"),
        Transcode::ApplyComponentVerb(Blueprint, TEXT("bp_component_remove"), Op, Error));
    Op->SetStringField(TEXT("prop"), TEXT("NotAProperty"));
    TestFalse(TEXT("unknown property is rejected"),
        Transcode::ApplyComponentVerb(Blueprint, TEXT("bp_component_set_prop"), Op, Error));
    TestEqual(TEXT("failed edits preserve override"), Child->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), 31.0f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusEnhancedInputClass,
    "Nexus.Issues3.Character.EnhancedInputClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusEnhancedInputClass::RunTest(const FString& Parameters)
{
    UClass* Short = ResolveBlueprintNodeClassForCreate(TEXT("EnhancedInputAction"));
    UClass* Prefixed = ResolveBlueprintNodeClassForCreate(TEXT("K2Node_EnhancedInputAction"));
    UClass* Full = ResolveBlueprintNodeClassForCreate(TEXT("/Script/InputBlueprintNodes.K2Node_EnhancedInputAction"));
    TestNotNull(TEXT("enhanced input class resolves"), Short);
    TestEqual(TEXT("prefix and alias resolve identically"), Prefixed, Short);
    TestEqual(TEXT("full path and alias resolve identically"), Full, Short);
    return true;
}

#endif
