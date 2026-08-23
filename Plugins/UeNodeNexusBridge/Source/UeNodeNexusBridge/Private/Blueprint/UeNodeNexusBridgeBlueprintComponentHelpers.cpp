#include "UeNodeNexusBridgeBlueprintComponentHelpers.h"

#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
USCS_Node* FindBlueprintSCSNode(
    USimpleConstructionScript* Script,
    const FString& Name)
{
    if (Script == nullptr || Name.IsEmpty())
    {
        return nullptr;
    }
    if (USCS_Node* ExactNode = Script->FindSCSNode(FName(*Name)))
    {
        return ExactNode;
    }
    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node != nullptr
            && Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }
    return nullptr;
}

USceneComponent* FindNativeBlueprintSceneComponent(
    UBlueprint* Blueprint,
    const FString& Name)
{
    UBlueprintGeneratedClass* GeneratedClass =
        Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get()) : nullptr;
    AActor* DefaultActor =
        GeneratedClass ? Cast<AActor>(GeneratedClass->GetDefaultObject()) : nullptr;
    if (DefaultActor == nullptr)
    {
        return nullptr;
    }
    if (Name.Equals(TEXT("RootComponent"), ESearchCase::IgnoreCase))
    {
        return DefaultActor->GetRootComponent();
    }
    for (TFieldIterator<FObjectProperty> It(
        GeneratedClass,
        EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectProperty* Property = *It;
        if (Property != nullptr
            && Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            if (USceneComponent* Component =
                Cast<USceneComponent>(Property->GetObjectPropertyValue_InContainer(DefaultActor)))
            {
                return Component;
            }
        }
    }
    TInlineComponentArray<USceneComponent*> Components(DefaultActor);
    for (USceneComponent* Component : Components)
    {
        if (Component != nullptr
            && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> MakeBlueprintComponentItem(
    USCS_Node* Node,
    const FString& ParentName)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(
        TEXT("name"),
        Node ? Node->GetVariableName().ToString() : FString());
    Item->SetStringField(
        TEXT("component_class"),
        Node && Node->ComponentClass ? Node->ComponentClass->GetPathName() : FString());
    Item->SetStringField(
        TEXT("template_path"),
        Node && Node->ComponentTemplate ? Node->ComponentTemplate->GetPathName() : FString());
    Item->SetStringField(TEXT("parent"), ParentName);
    return Item;
}
}
