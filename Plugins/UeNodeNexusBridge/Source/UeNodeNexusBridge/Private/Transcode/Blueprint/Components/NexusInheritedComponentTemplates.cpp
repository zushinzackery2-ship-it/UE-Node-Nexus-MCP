#include "NexusInheritedComponentTemplates.h"
#include "NexusNativeComponentTemplates.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"

namespace UeNodeNexusBridge::Transcode
{
USCS_Node* FindInheritedComponentNode(UBlueprint* Blueprint, const FName Name)
{
    for (UClass* Parent = Blueprint ? Blueprint->ParentClass.Get() : nullptr; Parent; Parent = Parent->GetSuperClass())
    {
        auto* Generated = Cast<UBlueprintGeneratedClass>(Parent);
        if (!Generated || !Generated->SimpleConstructionScript)
        {
            continue;
        }
        if (USCS_Node* Node = Generated->SimpleConstructionScript->FindSCSNode(Name))
        {
            return Node;
        }
    }
    return nullptr;
}

UActorComponent* ResolveInheritedComponentTemplate(UBlueprint* Blueprint, const FName Name, const bool bCreateOverride)
{
    if (auto* Native = FindNativeComponentTemplate(Blueprint, Name))
    {
        return Native;
    }
    USCS_Node* Node = FindInheritedComponentNode(Blueprint, Name);
    auto* Generated = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get()) : nullptr;
    if (!Node || !Generated)
    {
        return nullptr;
    }
    if (!bCreateOverride)
    {
        return Node->GetActualComponentTemplate(Generated);
    }
    if (!Node->ComponentTemplate || !Node->ComponentTemplate->bEditableWhenInherited)
    {
        return nullptr;
    }
    // An inherited SCS template belongs to its declaring Blueprint. Editing it
    // directly would change every sibling; UE stores child changes in a handler.
    auto* Handler = Blueprint->GetInheritableComponentHandler(true);
    if (!Handler)
    {
        return nullptr;
    }
    const FComponentKey Key(Node);
    if (auto* Override = Handler->GetOverridenComponentTemplate(Key))
    {
        return Override;
    }
    Handler->Modify();
    return Handler->CreateOverridenComponentTemplate(Key);
}
}
