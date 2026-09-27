#include "Blueprint/Variables/NexusBlueprintLocalVariables.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"

namespace UeNodeNexusBridge
{
const FBPVariableDescription* FindBlueprintLocalVariable(UEdGraph* Graph, FName Name)
{
    if (Graph == nullptr)
    {
        return nullptr;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    for (const UK2Node_FunctionEntry* Entry : Entries)
    {
        const FBPVariableDescription* Local = Entry->LocalVariables.FindByPredicate([Name](const FBPVariableDescription& Item)
        {
            return Item.VarName == Name;
        });
        if (Local != nullptr)
        {
            return Local;
        }
    }
    return nullptr;
}
}
