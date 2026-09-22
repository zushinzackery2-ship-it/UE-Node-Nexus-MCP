#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "Blueprint/NexusBlueprintVariableType.h"

#include "Components/ActorComponent.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
FBPVariableDescription* FindVariable(UBlueprint* Blueprint, const FName Name)
{
    for (FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        if (Variable.VarName == Name)
        {
            return &Variable;
        }
    }
    return nullptr;
}

void ApplyVariableFields(UBlueprint* Blueprint, const FName Name, const TSharedPtr<FJsonObject>& Op)
{
    FBPVariableDescription* Variable = FindVariable(Blueprint, Name);
    if (Variable == nullptr)
    {
        return;
    }
    FString Text;
    if (Op->TryGetStringField(TEXT("default"), Text))
    {
        // DefaultValue seeds the CDO on the next compile; an already compiled variable
        // lives on the CDO, so write there too (the export reads the CDO back).
        Variable->DefaultValue = Text;
        UObject* Cdo = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        FProperty* Property = Cdo ? FindFProperty<FProperty>(Cdo->GetClass(), Name) : nullptr;
        FString Error;
        if (Property != nullptr && Text.IsEmpty())
        {
            Cdo->Modify();
            Property->ClearValue_InContainer(Cdo);
        }
        else if (Property != nullptr)
        {
            ImportPropertyValue(Cdo, Name.ToString(), Text, Error);
        }
    }
    if (Op->TryGetStringField(TEXT("category"), Text))
    {
        FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, Name, nullptr, Text.IsEmpty() ? FText::GetEmpty() : FText::FromString(Text));
        Variable = FindVariable(Blueprint, Name);
    }
    if (Op->TryGetStringField(TEXT("tooltip"), Text))
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, FBlueprintMetadata::MD_Tooltip, Text);
        Variable = FindVariable(Blueprint, Name);
    }
    if (Op->HasField(TEXT("flags")) && Variable != nullptr)
    {
        bool bPrivate = false;
        bool bMultiline = false;
        const uint64 Flags = VariableFlagsFromList(ReadOpStrings(Op, TEXT("flags")), bPrivate, bMultiline);
        Variable->PropertyFlags = Flags;
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, FBlueprintMetadata::MD_Private, bPrivate ? TEXT("true") : TEXT("false"));
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, Name, nullptr, FName(TEXT("MultiLine")), bMultiline ? TEXT("true") : TEXT("false"));
        Variable = FindVariable(Blueprint, Name);
    }
    if (Op->TryGetStringField(TEXT("rep_notify"), Text) && Variable != nullptr)
    {
        Variable->RepNotifyFunc = Text.IsEmpty() ? NAME_None : FName(*Text);
        if (!Text.IsEmpty())
        {
            Variable->PropertyFlags |= CPF_Net | CPF_RepNotify;
        }
        else
        {
            Variable->PropertyFlags &= ~CPF_RepNotify;
        }
    }
}

USCS_Node* FindComponentNode(UBlueprint* Blueprint, const FString& Name)
{
    USimpleConstructionScript* Scs = Blueprint->SimpleConstructionScript;
    return Scs ? Scs->FindSCSNode(FName(*Name)) : nullptr;
}

bool AttachComponentNode(UBlueprint* Blueprint, USCS_Node* Node, const FString& Parent, const FString& Socket)
{
    USimpleConstructionScript* Scs = Blueprint->SimpleConstructionScript;
    if (USCS_Node* ParentNode = Scs->FindSCSNode(FName(*Parent)))
    {
        Node->bIsParentComponentNative = false;
        Node->ParentComponentOrVariableName = NAME_None;
        Node->ParentComponentOwnerClassName = NAME_None;
        ParentNode->AddChildNode(Node);
    }
    else if (Parent.IsEmpty())
    {
        Node->bIsParentComponentNative = false;
        Node->ParentComponentOrVariableName = NAME_None;
        Node->ParentComponentOwnerClassName = NAME_None;
        Scs->AddNode(Node);
    }
    else
    {
        Scs->AddNode(Node);
        Node->bIsParentComponentNative = true;
        Node->ParentComponentOrVariableName = FName(*Parent);
        Node->ParentComponentOwnerClassName = Blueprint->ParentClass ? Blueprint->ParentClass->GetFName() : NAME_None;
    }
    Node->AttachToName = Socket.IsEmpty() ? NAME_None : FName(*Socket);
    return true;
}
}

bool ApplyVariableVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    const FName Name(*ReadOpString(Op, TEXT("name")));
    if (Verb == TEXT("bp_variable_add"))
    {
        FEdGraphPinType Type;
        if (!ReadOpPinType(Op, Type, OutError))
        {
            return false;
        }
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type, ReadOpString(Op, TEXT("default"))))
        {
            OutError = FString::Printf(TEXT("AddMemberVariable failed for %s"), *Name.ToString());
            return false;
        }
        ApplyVariableFields(Blueprint, Name, Op);
        return true;
    }
    if (Verb == TEXT("bp_variable_set"))
    {
        if (FindVariable(Blueprint, Name) == nullptr)
        {
            OutError = FString::Printf(TEXT("variable not found: %s"), *Name.ToString());
            return false;
        }
        if (Op->HasField(TEXT("type")))
        {
            FEdGraphPinType Type;
            if (!ReadOpPinType(Op, Type, OutError))
            {
                return false;
            }
            if (!ChangeVariableType(Blueprint, Name, Type, OutError) || !VerifyVariableType(Blueprint, Name, Type, OutError))
            {
                return false;
            }
        }
        ApplyVariableFields(Blueprint, Name, Op);
        return true;
    }
    if (Verb == TEXT("bp_variable_remove"))
    {
        FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
        return true;
    }
    if (Verb == TEXT("bp_variable_rename"))
    {
        FBlueprintEditorUtils::RenameMemberVariable(Blueprint, FName(*ReadOpString(Op, TEXT("old"))), FName(*ReadOpString(Op, TEXT("new"))));
        return true;
    }
    OutError = FString::Printf(TEXT("unknown variable verb %s"), *Verb);
    return false;
}

bool ApplyComponentVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    USimpleConstructionScript* Scs = Blueprint->SimpleConstructionScript;
    if (Scs == nullptr)
    {
        OutError = TEXT("Blueprint has no SimpleConstructionScript (not an Actor Blueprint?)");
        return false;
    }
    const FString Name = ReadOpString(Op, TEXT("name"));
    if (Verb == TEXT("bp_component_add"))
    {
        UClass* Class = ResolveClassByNameOrPath(ReadOpString(Op, TEXT("class")));
        if (Class == nullptr || !Class->IsChildOf(UActorComponent::StaticClass()))
        {
            OutError = FString::Printf(TEXT("component class not found: %s"), *ReadOpString(Op, TEXT("class")));
            return false;
        }
        USCS_Node* Node = Scs->CreateNode(Class, FName(*Name));
        return AttachComponentNode(Blueprint, Node, ReadOpString(Op, TEXT("parent")), ReadOpString(Op, TEXT("socket")));
    }
    USCS_Node* Node = FindComponentNode(Blueprint, Verb == TEXT("bp_component_rename") ? ReadOpString(Op, TEXT("old")) : Name);
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("component not found in this Blueprint's construction script: %s (inherited components are read-only here)"), *Name);
        return false;
    }
    if (Verb == TEXT("bp_component_remove"))
    {
        Scs->RemoveNodeAndPromoteChildren(Node);
        return true;
    }
    if (Verb == TEXT("bp_component_set_prop"))
    {
        UActorComponent* Template = Node->ComponentTemplate;
        if (Template == nullptr)
        {
            OutError = FString::Printf(TEXT("component %s has no template"), *Name);
            return false;
        }
        return ImportPropertyValue(Template, ReadOpString(Op, TEXT("prop")), ReadOpString(Op, TEXT("value")), OutError);
    }
    if (Verb == TEXT("bp_component_reparent"))
    {
        if (USCS_Node* OldParent = Scs->FindParentNode(Node))
        {
            OldParent->RemoveChildNode(Node, false);
        }
        else
        {
            Scs->RemoveNode(Node, false);
        }
        return AttachComponentNode(Blueprint, Node, ReadOpString(Op, TEXT("parent")), ReadOpString(Op, TEXT("socket")));
    }
    if (Verb == TEXT("bp_component_rename"))
    {
        FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Node, FName(*ReadOpString(Op, TEXT("new"))));
        return true;
    }
    OutError = FString::Printf(TEXT("unknown component verb %s"), *Verb);
    return false;
}
}
