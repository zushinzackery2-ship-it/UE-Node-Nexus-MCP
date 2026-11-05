#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Transcode/Blueprint/Apply/UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "Transcode/Blueprint/Components/NexusInheritedComponentTemplates.h"
#include "UeNodeNexusBridgeTranscode.h"

namespace
{
UBlueprint* InheritedBlueprint(UClass* Parent, const TCHAR* Label)
{
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), Label);
    return FKismetEditorUtilities::CreateBlueprint(Parent, GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusInheritedScsRoundtrip,
    "Nexus.ContentStructure.Blueprint.InheritedScsRoundtrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusInheritedScsRoundtrip::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Transcode;
    UBlueprint* Parent = InheritedBlueprint(AActor::StaticClass(), TEXT("NexusScsParent"));
    USCS_Node* Root = Parent->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), TEXT("Root"));
    Parent->SimpleConstructionScript->AddNode(Root);
    FKismetEditorUtilities::CompileBlueprint(Parent);
    UBlueprint* Middle = InheritedBlueprint(Parent->GeneratedClass, TEXT("NexusScsMiddle"));
    FKismetEditorUtilities::CompileBlueprint(Middle);
    UBlueprint* Child = InheritedBlueprint(Middle->GeneratedClass, TEXT("NexusScsChild"));

    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("name"), TEXT("Root"));
    Request->SetStringField(TEXT("class"), TEXT("/Script/Engine.SceneComponent"));
    FString Error;
    if (!TestTrue(TEXT("ancestor SCS identity binds across two generations"),
        ApplyComponentVerb(Child, TEXT("bp_component_bind"), Request, Error)))
    {
        AddError(Error);
        return false;
    }
    Request->SetStringField(TEXT("name"), TEXT("LocalView"));
    Request->SetStringField(TEXT("parent"), TEXT("Root"));
    TestTrue(TEXT("local component attaches to inherited SCS root"),
        ApplyComponentVerb(Child, TEXT("bp_component_add"), Request, Error));
    FKismetEditorUtilities::CompileBlueprint(Child);
    const USCS_Node* View = Child->SimpleConstructionScript->FindSCSNode(TEXT("LocalView"));
    TestTrue(TEXT("attachment stores declaring class and SCS identity"), View && !View->bIsParentComponentNative
        && View->ParentComponentOrVariableName == TEXT("Root")
        && View->ParentComponentOwnerClassName == Parent->GeneratedClass->GetFName());

    Request->SetStringField(TEXT("name"), TEXT("Root"));
    Request->SetStringField(TEXT("prop"), TEXT("RelativeLocation"));
    Request->SetStringField(TEXT("value"), TEXT("(X=12,Y=0,Z=0)"));
    TestTrue(TEXT("child override uses an inheritable component handler"),
        ApplyComponentVerb(Child, TEXT("bp_component_set_prop"), Request, Error));
    FKismetEditorUtilities::CompileBlueprint(Child);
    auto* ChildRoot = Cast<USceneComponent>(ResolveInheritedComponentTemplate(Child, TEXT("Root")));
    auto* ParentRoot = Cast<USceneComponent>(Root->ComponentTemplate);
    TestTrue(TEXT("override persists through compile"), ChildRoot && ChildRoot->GetRelativeLocation().X == 12.0);
    TestTrue(TEXT("ancestor template is preserved"), ParentRoot && ParentRoot->GetRelativeLocation().IsZero());
    TestTrue(TEXT("child has its own template"), ChildRoot && ChildRoot != ParentRoot);

    const auto Raw = BuildBlueprintRaw(Child);
    const auto& Components = Raw->GetObjectField(TEXT("blueprint"))->GetArrayField(TEXT("components"));
    TSet<FString> Names;
    for (const auto& Value : Components)
    {
        const auto Row = Value->AsObject();
        Names.Add(Row->GetStringField(TEXT("name")));
        if (Row->GetStringField(TEXT("name")) == TEXT("Root"))
        {
            TestTrue(TEXT("raw export marks ancestor component inherited"), Row->GetBoolField(TEXT("inherited")));
            bool bFoundOverride = false;
            for (const auto& Property : Row->GetArrayField(TEXT("props")))
            {
                const auto Prop = Property->AsObject();
                if (Prop->GetStringField(TEXT("name")) == TEXT("RelativeLocation"))
                {
                    bFoundOverride = Prop->GetStringField(TEXT("value")) != Prop->GetStringField(TEXT("default"));
                }
            }
            TestTrue(TEXT("raw export binds the override to its parent default"), bFoundOverride);
        }
    }
    TestTrue(TEXT("raw export contains both local and ancestor identity"), Names.Contains(TEXT("Root")) && Names.Contains(TEXT("LocalView")));
    TestEqual(TEXT("inherited identity appears once"), Names.Num(), Components.Num());
    Request->SetStringField(TEXT("class"), TEXT("/Script/Engine.CameraComponent"));
    TestFalse(TEXT("inherited class mismatch remains rejected"), ApplyComponentVerb(Child, TEXT("bp_component_bind"), Request, Error));
    return true;
}
#endif
