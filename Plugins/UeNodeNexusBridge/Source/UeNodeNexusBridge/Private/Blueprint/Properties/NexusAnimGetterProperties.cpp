#include "NexusAnimGetterProperties.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Dom/JsonValue.h"
#include "K2Node_AnimGetter.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool IsAnimGetterBindingProperty(const UEdGraphNode* Node, const FProperty* Property)
{
    if (!Cast<UK2Node_AnimGetter>(Node) || !Property ||
        Property->GetOwnerClass() != UK2Node_AnimGetter::StaticClass())
    {
        return false;
    }
    const FName Name = Property->GetFName();
    return Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, SourceNode) ||
        Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, SourceStateNode) ||
        Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, GetterClass) ||
        Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, SourceAnimBlueprint) ||
        Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, CachedTitle) ||
        Name == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, Contexts);
}

bool ValidateAnimGetterBinding(const UEdGraphNode* Node, const FProperty* Property,
    const TSharedPtr<FJsonValue>& Value, FString& Error)
{
    if (!IsAnimGetterBindingProperty(Node, Property) || !CastField<FObjectPropertyBase>(Property))
    {
        return true;
    }
    FString Path;
    if (Value->Type == EJson::Null || (Value->TryGetString(Path) &&
        (Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase))))
    {
        return true;
    }
    UObject* Object = ResolveObjectByPath(Path);
    if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UK2Node_AnimGetter, GetterClass))
    {
        const UClass* Class = Cast<UClass>(Object);
        if (!Class || !Class->IsChildOf(UAnimInstance::StaticClass()))
        {
            Error = TEXT("anim_getter_requires_anim_instance_class");
            return false;
        }
        return true;
    }
    const UAnimBlueprint* Owner = Node->GetTypedOuter<UAnimBlueprint>();
    const UAnimBlueprint* Target = Cast<UAnimBlueprint>(Object);
    if (!Target && Object)
    {
        Target = Object->GetTypedOuter<UAnimBlueprint>();
    }
    if (!Owner || Owner != Target)
    {
        Error = TEXT("anim_getter_source_must_share_blueprint");
        return false;
    }
    return true;
}
}
