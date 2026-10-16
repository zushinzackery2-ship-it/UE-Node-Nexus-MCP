#include "NexusDelegateBinding.h"

#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallDelegate.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeBlueprintNodeCreateFields.h"

namespace UeNodeNexusBridge
{
bool ConfigureDelegateBinding(UK2Node_BaseMCDelegate* Node, UBlueprint* Blueprint,
    const TSharedPtr<FJsonObject>& Payload, bool bDryRun, FString& OutError)
{
    FString Name;
    FString Owner;
    if (!ReadBlueprintNodeStringField(Payload, TEXT("delegate_name"), Name) || Name.IsEmpty()
        || !ReadBlueprintNodeStringField(Payload, TEXT("delegate_owner"), Owner) || Owner.IsEmpty())
    {
        OutError = TEXT("delegate_name and delegate_owner are required for multicast delegate nodes");
        return false;
    }
    const bool bSelfContext = Owner == TEXT("self");
    UClass* OwnerClass = nullptr;
    if (bSelfContext)
    {
        OwnerClass = Blueprint ? Blueprint->SkeletonGeneratedClass.Get() : nullptr;
        if (OwnerClass == nullptr && Blueprint)
        {
            OwnerClass = Blueprint->GeneratedClass.Get();
        }
    }
    else
    {
        OwnerClass = Owner.StartsWith(TEXT("/")) ? LoadObject<UClass>(nullptr, *Owner)
            : UClass::TryFindTypeSlow<UClass>(Owner);
    }
    FMulticastDelegateProperty* Property = OwnerClass
        ? FindFProperty<FMulticastDelegateProperty>(OwnerClass, FName(*Name)) : nullptr;
    if (Property == nullptr || Property->SignatureFunction == nullptr)
    {
        OutError = FString::Printf(TEXT("multicast delegate not found: %s.%s"), *Owner, *Name);
        return false;
    }
    const EPropertyFlags Required = Node->IsA<UK2Node_CallDelegate>()
        ? CPF_BlueprintCallable : CPF_BlueprintAssignable;
    if (!bSelfContext && !Property->HasAnyPropertyFlags(Required))
    {
        OutError = FString::Printf(TEXT("multicast delegate is not exposed for this operation: %s.%s"), *Owner, *Name);
        return false;
    }
    if (!bDryRun)
    {
        Node->SetFromProperty(Property, bSelfContext, OwnerClass);
    }
    return true;
}
}
