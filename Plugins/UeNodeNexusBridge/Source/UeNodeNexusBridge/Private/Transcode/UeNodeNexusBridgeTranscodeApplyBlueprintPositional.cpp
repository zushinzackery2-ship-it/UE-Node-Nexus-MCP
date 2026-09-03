#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"

#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_StructOperation.h"
#include "K2Node_Variable.h"

namespace UeNodeNexusBridge::Transcode
{
UClass* ResolveClassByNameOrPath(const FString& Text)
{
    if (Text.IsEmpty())
    {
        return nullptr;
    }
    if (Text.StartsWith(TEXT("/")))
    {
        if (UClass* Loaded = LoadObject<UClass>(nullptr, *Text))
        {
            return Loaded;
        }
        return Text.EndsWith(TEXT("_C")) ? nullptr : LoadObject<UClass>(nullptr, *(Text + TEXT("_C")));
    }
    return UClass::TryFindTypeSlow<UClass>(Text);
}

bool ConfigureFromPositional(UBlueprint* Blueprint, UClass* NodeClass, const TArray<FString>& Positional, const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
    const FString First = Positional.Num() > 0 ? Positional[0] : FString();
    if (NodeClass->IsChildOf(UK2Node_CustomEvent::StaticClass()))
    {
        Config->SetStringField(TEXT("event_name"), First);
        return true;
    }
    if (NodeClass->IsChildOf(UK2Node_Event::StaticClass()) || NodeClass->IsChildOf(UK2Node_CallFunction::StaticClass()))
    {
        FString Owner;
        FString Function;
        if (!First.Split(TEXT("."), &Owner, &Function, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            OutError = FString::Printf(TEXT("%s needs Owner.Function, got %s"), *NodeClass->GetName(), *First);
            return false;
        }
        UClass* OwnerClass = nullptr;
        if (Owner == TEXT("self"))
        {
            OwnerClass = Blueprint->SkeletonGeneratedClass ? Blueprint->SkeletonGeneratedClass.Get() : Blueprint->GeneratedClass.Get();
        }
        else
        {
            OwnerClass = ResolveClassByNameOrPath(Owner);
        }
        if (OwnerClass == nullptr)
        {
            OutError = FString::Printf(TEXT("class not found: %s"), *Owner);
            return false;
        }
        Config->SetStringField(TEXT("function_owner"), OwnerClass->GetPathName());
        Config->SetStringField(TEXT("function_name"), Function);
        return true;
    }
    if (NodeClass->IsChildOf(UK2Node_Variable::StaticClass()))
    {
        const int32 Dot = First.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        Config->SetStringField(TEXT("variable_name"), Dot >= 0 ? First.RightChop(Dot + 1) : First);
        if (Dot >= 0)
        {
            Config->SetStringField(TEXT("variable_owner"), First.Left(Dot));
        }
        return true;
    }
    if (NodeClass->IsChildOf(UK2Node_MacroInstance::StaticClass()))
    {
        Config->SetStringField(TEXT("macro"), First);
        return true;
    }
    if (NodeClass->IsChildOf(UK2Node_DynamicCast::StaticClass()))
    {
        Config->SetStringField(TEXT("target_type"), First);
        return true;
    }
    if (NodeClass->IsChildOf(UK2Node_StructOperation::StaticClass()))
    {
        Config->SetStringField(TEXT("struct_type"), First);
        return true;
    }
    const FString ClassName = NodeClass->GetName();
    if (ClassName == TEXT("K2Node_InputKey"))
    {
        Config->SetStringField(TEXT("input_key"), First);
    }
    else if (ClassName == TEXT("K2Node_InputAction"))
    {
        Config->SetStringField(TEXT("input_action_name"), First);
    }
    else if (ClassName == TEXT("K2Node_InputAxisEvent"))
    {
        Config->SetStringField(TEXT("axis_name"), First);
    }
    else if (ClassName == TEXT("K2Node_EnhancedInputAction"))
    {
        Config->SetStringField(TEXT("input_action"), First);
    }
    return true;
}
}
