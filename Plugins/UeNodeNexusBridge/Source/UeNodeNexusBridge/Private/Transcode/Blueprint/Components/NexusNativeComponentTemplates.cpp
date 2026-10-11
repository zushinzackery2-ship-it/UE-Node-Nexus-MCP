#include "NexusNativeComponentTemplates.h"

#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
TArray<FString> NativeComponentAliases(const UActorComponent* Component)
{
    TArray<FString> Aliases;
    const auto* Cdo = Component ? Cast<AActor>(Component->GetOuter()) : nullptr;
    if (!Cdo || Component->CreationMethod != EComponentCreationMethod::Native)
    {
        return Aliases;
    }
    Aliases.Add(Component->GetName());
    for (TFieldIterator<FObjectPropertyBase> It(Cdo->GetClass()); It; ++It)
    {
        if (It->GetObjectPropertyValue_InContainer(Cdo) == Component)
        {
            Aliases.AddUnique(It->GetName());
        }
    }
    return Aliases;
}

UActorComponent* FindNativeComponentTemplate(UBlueprint* Blueprint, const FName Name)
{
    AActor* Cdo = Blueprint && Blueprint->GeneratedClass
        ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
    if (Cdo == nullptr)
    {
        return nullptr;
    }
    if (const auto* Property = CastField<FObjectPropertyBase>(Cdo->GetClass()->FindPropertyByName(Name)))
    {
        auto* Component = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(Cdo));
        if (Component && Component->CreationMethod == EComponentCreationMethod::Native && Component->GetOuter() == Cdo)
        {
            return Component;
        }
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
