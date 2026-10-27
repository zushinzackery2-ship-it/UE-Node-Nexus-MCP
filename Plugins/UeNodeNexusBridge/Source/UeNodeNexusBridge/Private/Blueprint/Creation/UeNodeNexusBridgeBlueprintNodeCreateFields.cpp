#include "UeNodeNexusBridgeBlueprintNodeCreateFields.h"

#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool ReadBlueprintNodeStringField(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& FieldName,
    FString& OutValue)
{
    if (Payload->TryGetStringField(FieldName, OutValue))
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    return Payload->TryGetObjectField(TEXT("params"), Params)
        && Params != nullptr
        && (*Params)->TryGetStringField(FieldName, OutValue);
}

bool ReadBlueprintNodeBoolField(
    const TSharedPtr<FJsonObject>& Payload,
    const FString& FieldName,
    bool& OutValue)
{
    if (Payload->TryGetBoolField(FieldName, OutValue))
    {
        return true;
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    return Payload->TryGetObjectField(TEXT("params"), Params)
        && Params != nullptr
        && (*Params)->TryGetBoolField(FieldName, OutValue);
}

bool ReadBlueprintVariableName(
    const TSharedPtr<FJsonObject>& Payload,
    FString& OutValue)
{
    if (ReadBlueprintNodeStringField(Payload, TEXT("variable_name"), OutValue)
        || ReadBlueprintNodeStringField(Payload, TEXT("VariableName"), OutValue)
        || ReadBlueprintNodeStringField(Payload, TEXT("member_name"), OutValue))
    {
        return !OutValue.IsEmpty();
    }

    const TSharedPtr<FJsonObject>* VariableReference = nullptr;
    if (Payload->TryGetObjectField(TEXT("variable_reference"), VariableReference)
        && VariableReference != nullptr)
    {
        return (*VariableReference)->TryGetStringField(TEXT("member_name"), OutValue)
            && !OutValue.IsEmpty();
    }

    const TSharedPtr<FJsonObject>* Params = nullptr;
    if (Payload->TryGetObjectField(TEXT("params"), Params) && Params != nullptr)
    {
        if ((*Params)->TryGetObjectField(TEXT("variable_reference"), VariableReference)
            && VariableReference != nullptr)
        {
            return (*VariableReference)->TryGetStringField(TEXT("member_name"), OutValue)
                && !OutValue.IsEmpty();
        }
    }
    return false;
}

USCS_Node* FindBlueprintComponentNode(UBlueprint* Blueprint, FName VariableName)
{
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    if (Script == nullptr || VariableName.IsNone())
    {
        return nullptr;
    }
    if (USCS_Node* ExactNode = Script->FindSCSNode(VariableName))
    {
        return ExactNode;
    }
    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node != nullptr
            && Node->GetVariableName().IsEqual(VariableName, ENameCase::IgnoreCase))
        {
            return Node;
        }
    }
    return nullptr;
}

FProperty* FindBlueprintProperty(UBlueprint* Blueprint, FName VariableName)
{
    if (Blueprint == nullptr || VariableName.IsNone())
    {
        return nullptr;
    }
    if (UClass* SkeletonClass = Blueprint->SkeletonGeneratedClass)
    {
        if (FProperty* Property = FindFProperty<FProperty>(SkeletonClass, VariableName))
        {
            return Property;
        }
    }
    if (UClass* GeneratedClass = Blueprint->GeneratedClass)
    {
        if (FProperty* Property = FindFProperty<FProperty>(GeneratedClass, VariableName))
        {
            return Property;
        }
    }
    return nullptr;
}

bool BlueprintVariableExists(UBlueprint* Blueprint, const FString& VariableName)
{
    const FName VariableFName(*VariableName);
    return FindBlueprintProperty(Blueprint, VariableFName) != nullptr
        || FindBlueprintComponentNode(Blueprint, VariableFName) != nullptr;
}
}
