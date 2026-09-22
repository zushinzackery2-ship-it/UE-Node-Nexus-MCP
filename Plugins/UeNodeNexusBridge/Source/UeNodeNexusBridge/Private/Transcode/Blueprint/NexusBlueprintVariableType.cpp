#include "NexusBlueprintVariableType.h"

#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
FBPVariableDescription* FindMemberVariable(UBlueprint* Blueprint, const FName Name)
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

// FBlueprintEditorUtils keeps its own versions of the next two helpers
// protected, so they are rebuilt here from the public API they are written in
// terms of. Both follow the engine's rule exactly: a node counts when it says it
// references the variable, and a Blueprint counts when the one being edited is
// somewhere in its parent hierarchy or among the interfaces it implements.
TArray<UK2Node*> NodesForVariable(const UBlueprint* Blueprint, const FName Name)
{
    TArray<UK2Node*> Nodes;
    FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node>(Blueprint, Nodes);
    return Nodes.FilterByPredicate([Name](const UK2Node* Node)
    {
        return Node != nullptr && Node->ReferencesVariable(Name, nullptr);
    });
}

TArray<UBlueprint*> LoadedChildBlueprints(UBlueprint* Blueprint)
{
    TArray<UBlueprint*> Children;
    for (TObjectIterator<UBlueprint> It; It; ++It)
    {
        UBlueprint* Child = *It;
        if (Child == nullptr || Child->ParentClass == nullptr)
        {
            continue;
        }
        TArray<UBlueprint*> Parents;
        UBlueprint::GetBlueprintHierarchyFromClass(Child->ParentClass, Parents);
        TArray<UClass*> Interfaces;
        FBlueprintEditorUtils::FindImplementedInterfaces(Child, true, Interfaces);
        for (UClass* Interface : Interfaces)
        {
            if (UBlueprint* Owner = UBlueprint::GetBlueprintFromClass(Interface))
            {
                Parents.Add(Owner);
            }
        }
        if (Parents.Contains(Blueprint))
        {
            Children.Add(Child);
        }
    }
    return Children;
}

void ApplyObjectTemplateFlag(FBPVariableDescription& Variable, const FEdGraphPinType& NewType)
{
    const bool bObject = NewType.PinCategory == UEdGraphSchema_K2::PC_Object || NewType.PinCategory == UEdGraphSchema_K2::PC_Interface;
    const UClass* Target = bObject ? Cast<UClass>(NewType.PinSubCategoryObject.Get()) : nullptr;
    if (Target != nullptr && Target->IsChildOf(AActor::StaticClass()))
    {
        Variable.PropertyFlags |= CPF_DisableEditOnTemplate;
        return;
    }
    Variable.PropertyFlags &= ~CPF_DisableEditOnTemplate;
}
}

bool ChangeVariableType(UBlueprint* Blueprint, const FName Name, const FEdGraphPinType& NewType, FString& OutError)
{
    // FBlueprintEditorUtils::ChangeMemberVariableType asks for confirmation
    // through FSuppressableWarningDialog whenever the variable already has nodes.
    // With nobody to answer, ShowModal returns Cancel and the engine returns
    // without changing anything and without reporting anything, so a scalar ->
    // Array change was accepted by the plan, committed to the mirror, and simply
    // absent from the asset until compilation failed on undetermined wildcards.
    FBPVariableDescription* Variable = FindMemberVariable(Blueprint, Name);
    if (Variable == nullptr)
    {
        OutError = FString::Printf(TEXT("variable not found: %s"), *Name.ToString());
        return false;
    }
    if (Variable->VarType == NewType)
    {
        return true;
    }
    if ((NewType.PinCategory == UEdGraphSchema_K2::PC_Object || NewType.PinCategory == UEdGraphSchema_K2::PC_Interface)
        && Cast<UClass>(NewType.PinSubCategoryObject.Get()) == nullptr)
    {
        OutError = FString::Printf(TEXT("%s needs a resolvable class for an object variable"), *Name.ToString());
        return false;
    }
    Blueprint->Modify();
    ApplyObjectTemplateFlag(*Variable, NewType);
    const TArray<UBlueprint*> Children = LoadedChildBlueprints(Blueprint);
    TArray<UK2Node*> Nodes = NodesForVariable(Blueprint, Name);
    for (const UBlueprint* Child : Children)
    {
        Nodes.Append(NodesForVariable(Child, Name));
    }
    const bool bBecameBoolean = Variable->VarType.PinCategory != UEdGraphSchema_K2::PC_Boolean && NewType.PinCategory == UEdGraphSchema_K2::PC_Boolean;
    const bool bBecameNotBoolean = Variable->VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean && NewType.PinCategory != UEdGraphSchema_K2::PC_Boolean;
    if (bBecameBoolean || bBecameNotBoolean)
    {
        Variable->FriendlyName = FName::NameToDisplayString(Variable->VarName.ToString(), bBecameBoolean);
    }
    Variable->VarType = NewType;
    if (NewType.IsSet() || NewType.IsMap())
    {
        // Sets and maps cannot replicate; leaving the flags would produce a
        // variable the compiler rejects for a reason the text never stated.
        Variable->PropertyFlags &= ~(CPF_Net | CPF_RepNotify);
        Variable->RepNotifyFunc = NAME_None;
        Variable->ReplicationCondition = COND_None;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    for (UBlueprint* Child : Children)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Child);
    }
    for (UK2Node* Node : Nodes)
    {
        if (Node != nullptr)
        {
            Schema->ReconstructNode(*Node, true);
        }
    }
    return true;
}

bool VerifyVariableType(UBlueprint* Blueprint, const FName Name, const FEdGraphPinType& NewType, FString& OutError)
{
    const FBPVariableDescription* Variable = FindMemberVariable(Blueprint, Name);
    if (Variable != nullptr && Variable->VarType == NewType)
    {
        return true;
    }
    // The failure this replaces was a write that reported success, so the write
    // reads itself back before the plan may call the change applied.
    OutError = FString::Printf(TEXT("variable %s kept its previous type; the requested type was not applied"), *Name.ToString());
    return false;
}
}
