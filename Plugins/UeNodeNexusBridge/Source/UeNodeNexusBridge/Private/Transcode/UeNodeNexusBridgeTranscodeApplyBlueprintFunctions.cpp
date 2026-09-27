#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "Blueprint/Members/NexusBlueprintSignaturePins.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
bool ApplyBlueprintFunctionSignature(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Signature, FString& OutError)
{
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    if (Entries.Num() == 0)
    {
        OutError = FString::Printf(TEXT("function graph %s has no entry node"), *Graph->GetName());
        return false;
    }
    UK2Node_FunctionEntry* Entry = Entries[0];
    const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    Signature->TryGetArrayField(TEXT("inputs"), Inputs);
    Signature->TryGetArrayField(TEXT("outputs"), Outputs);
    const bool bInherited = !Entry->IsEditable();
    if (!SetSignaturePins(Entry, Inputs, EGPD_Output, bInherited, OutError))
    {
        return false;
    }
    if (Outputs != nullptr && Outputs->Num() > 0)
    {
        UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
        if (!SetSignaturePins(Result, Outputs, EGPD_Input, bInherited, OutError))
        {
            return false;
        }
    }
    else
    {
        TArray<UK2Node_FunctionResult*> Results;
        Graph->GetNodesOfClass(Results);
        for (UK2Node_FunctionResult* Result : Results)
        {
            if (!SetSignaturePins(Result, nullptr, EGPD_Input, bInherited, OutError))
            {
                return false;
            }
        }
    }
    int32 Flags = Entry->GetExtraFlags() & ~(FUNC_BlueprintPure | FUNC_Const | FUNC_Static | FUNC_Public | FUNC_Protected | FUNC_Private);
    Entry->MetaData.bCallInEditor = false;
    for (const FString& Flag : ReadOpStrings(Signature, TEXT("flags")))
    {
        if (Flag == TEXT("Pure"))
        {
            Flags |= FUNC_BlueprintPure;
        }
        else if (Flag == TEXT("Const"))
        {
            Flags |= FUNC_Const;
        }
        else if (Flag == TEXT("Static"))
        {
            Flags |= FUNC_Static;
        }
        else if (Flag == TEXT("Protected"))
        {
            Flags |= FUNC_Protected;
        }
        else if (Flag == TEXT("Private"))
        {
            Flags |= FUNC_Private;
        }
        else if (Flag == TEXT("Public"))
        {
            Flags |= FUNC_Public;
        }
        else if (Flag == TEXT("CallInEditor"))
        {
            Entry->MetaData.bCallInEditor = true;
        }
    }
    Entry->SetExtraFlags(Flags);
    FString Category;
    if (Signature->TryGetStringField(TEXT("category"), Category))
    {
        Entry->MetaData.Category = FText::FromString(Category);
    }
    return OutError.IsEmpty();
}

bool ApplyFunctionVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    const FString Name = ReadOpString(Op, TEXT("name"));
    const TSharedPtr<FJsonObject>* Signature = nullptr;
    Op->TryGetObjectField(TEXT("signature"), Signature);
    if (Verb == TEXT("bp_function_add") || Verb == TEXT("bp_graph_add"))
    {
        if (UEdGraph* Existing = FindBlueprintGraph(Blueprint, Name))
        {
            const bool bInterface = Blueprint->ImplementedInterfaces.ContainsByPredicate([Existing](const FBPInterfaceDescription& Interface)
            {
                return Interface.Graphs.Contains(Existing);
            });
            if (bInterface && Verb == TEXT("bp_function_add"))
            {
                return Signature == nullptr || ApplyBlueprintFunctionSignature(Existing, *Signature, OutError);
            }
            OutError = TEXT("graph already exists: ") + Name;
            return false;
        }
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (Verb == TEXT("bp_graph_add"))
        {
            FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
            return true;
        }
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
        return Signature == nullptr || ApplyBlueprintFunctionSignature(Graph, *Signature, OutError);
    }
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, Name);
    if (Graph == nullptr)
    {
        if (Verb == TEXT("bp_function_remove"))
        {
            return true;
        }
        OutError = FString::Printf(TEXT("function not found: %s"), *Name);
        return false;
    }
    if (Verb == TEXT("bp_function_remove"))
    {
        FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
        return true;
    }
    if (Verb == TEXT("bp_function_signature_set"))
    {
        return Signature != nullptr && ApplyBlueprintFunctionSignature(Graph, *Signature, OutError);
    }
    if (Verb == TEXT("bp_function_rename"))
    {
        FBlueprintEditorUtils::RenameGraph(Graph, ReadOpString(Op, TEXT("new")));
        return true;
    }
    OutError = FString::Printf(TEXT("unknown function verb %s"), *Verb);
    return false;
}

bool ApplyLocalVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, ReadOpString(Op, TEXT("function")));
    if (Graph == nullptr)
    {
        OutError = FString::Printf(TEXT("function not found: %s"), *ReadOpString(Op, TEXT("function")));
        return false;
    }
    TArray<UK2Node_FunctionEntry*> Entries;
    Graph->GetNodesOfClass(Entries);
    if (Entries.Num() == 0)
    {
        OutError = TEXT("function has no entry node");
        return false;
    }
    UK2Node_FunctionEntry* Entry = Entries[0];
    const FName Name(*ReadOpString(Op, TEXT("name")));
    FBPVariableDescription* Local = Entry->LocalVariables.FindByPredicate([Name](const FBPVariableDescription& Item)
    {
        return Item.VarName == Name;
    });
    if (Verb == TEXT("bp_local_variable_remove"))
    {
        if (Local != nullptr)
        {
            Entry->Modify();
            Entry->LocalVariables.RemoveAll([Name](const FBPVariableDescription& Item)
            {
                return Item.VarName == Name;
            });
        }
        return true;
    }
    FEdGraphPinType Type;
    if (!ReadOpPinType(Op, Type, OutError))
    {
        return false;
    }
    if (Verb == TEXT("bp_local_variable_add") && Local == nullptr)
    {
        return FBlueprintEditorUtils::AddLocalVariable(Blueprint, Graph, Name, Type, ReadOpString(Op, TEXT("default")));
    }
    if (Local == nullptr)
    {
        OutError = FString::Printf(TEXT("local variable not found: %s"), *Name.ToString());
        return false;
    }
    Entry->Modify();
    Local->VarType = Type;
    Local->DefaultValue = ReadOpString(Op, TEXT("default"));
    return true;
}
}
