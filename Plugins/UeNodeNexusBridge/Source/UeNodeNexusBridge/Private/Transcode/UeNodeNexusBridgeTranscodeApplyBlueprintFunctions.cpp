#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
void SetUserPins(UK2Node_EditablePinBase* Node, const TArray<TSharedPtr<FJsonValue>>* Params, EEdGraphPinDirection Direction, FString& OutError)
{
    if (Node == nullptr)
    {
        return;
    }
    TArray<TSharedPtr<FUserPinInfo>> Existing = Node->UserDefinedPins;
    for (const TSharedPtr<FUserPinInfo>& Pin : Existing)
    {
        if (Pin.IsValid())
        {
            Node->RemoveUserDefinedPinByName(Pin->PinName);
        }
    }
    if (Params == nullptr)
    {
        return;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        const TSharedPtr<FJsonObject> Param = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!Param.IsValid())
        {
            continue;
        }
        FEdGraphPinType Type;
        FString Error;
        if (!ReadOpPinType(Param, Type, Error))
        {
            OutError = Error;
            continue;
        }
        UEdGraphPin* Created = Node->CreateUserDefinedPin(FName(*ReadOpString(Param, TEXT("name"))), Type, Direction);
        FString Default;
        if (Created && Param->TryGetStringField(TEXT("default"), Default) && !Default.IsEmpty() && Node->UserDefinedPins.Num() > 0)
        {
            Node->ModifyUserDefinedPinDefaultValue(Node->UserDefinedPins.Last(), Default);
        }
    }
}

bool ApplySignature(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Signature, FString& OutError)
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
    SetUserPins(Entry, Inputs, EGPD_Output, OutError);
    if (Outputs != nullptr && Outputs->Num() > 0)
    {
        UK2Node_FunctionResult* Result = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(Entry);
        SetUserPins(Result, Outputs, EGPD_Input, OutError);
    }
    int32 Flags = Entry->GetExtraFlags() & ~(FUNC_BlueprintPure | FUNC_Const | FUNC_Static | FUNC_Public | FUNC_Protected | FUNC_Private);
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
}

bool ApplyFunctionVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& OutError)
{
    const FString Name = ReadOpString(Op, TEXT("name"));
    const TSharedPtr<FJsonObject>* Signature = nullptr;
    Op->TryGetObjectField(TEXT("signature"), Signature);
    if (Verb == TEXT("bp_function_add") || Verb == TEXT("bp_graph_add"))
    {
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (Verb == TEXT("bp_graph_add"))
        {
            FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
            return true;
        }
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
        return Signature == nullptr || ApplySignature(Graph, *Signature, OutError);
    }
    UEdGraph* Graph = FindBlueprintGraph(Blueprint, Name);
    if (Graph == nullptr)
    {
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
        return Signature != nullptr && ApplySignature(Graph, *Signature, OutError);
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
    FBPVariableDescription* Local = Entry->LocalVariables.FindByPredicate([Name](const FBPVariableDescription& Item) { return Item.VarName == Name; });
    if (Verb == TEXT("bp_local_variable_remove"))
    {
        if (Local != nullptr)
        {
            Entry->Modify();
            Entry->LocalVariables.RemoveAll([Name](const FBPVariableDescription& Item) { return Item.VarName == Name; });
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
