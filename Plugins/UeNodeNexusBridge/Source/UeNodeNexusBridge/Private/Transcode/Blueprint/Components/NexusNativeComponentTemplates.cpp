#include "NexusNativeComponentTemplates.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"

namespace UeNodeNexusBridge::Transcode
{
UActorComponent* FindNativeComponentTemplate(UBlueprint* Blueprint, const FName Name)
{
    AActor* Cdo = Blueprint && Blueprint->GeneratedClass
        ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
    if (Cdo == nullptr)
    {
        return nullptr;
    }
    TInlineComponentArray<UActorComponent*> Components;
    Cdo->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component && Component->GetFName() == Name
            && Component->CreationMethod == EComponentCreationMethod::Native
            && Component->GetOuter() == Cdo)
        {
            return Component;
        }
    }
    return nullptr;
}
}
