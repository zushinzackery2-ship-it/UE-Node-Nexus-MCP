#include "UeNodeNexusBridgeTranscodeBlueprintApply.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace UeNodeNexusBridge::Transcode
{
bool ApplyInterfaceVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& Error)
{
    const FString Path = ReadOpString(Op, TEXT("interface"));
    UClass* Interface = LoadObject<UClass>(nullptr, *Path);
    if (Interface == nullptr || !Interface->HasAnyClassFlags(CLASS_Interface))
    {
        Error = TEXT("interface class not found: ") + Path;
        return false;
    }
    const bool bImplemented = Blueprint->ImplementedInterfaces.ContainsByPredicate([Interface](const FBPInterfaceDescription& Item)
    {
        return Item.Interface == Interface;
    });
    if (Verb == TEXT("bp_interface_remove"))
    {
        if (bImplemented)
        {
            FBlueprintEditorUtils::RemoveInterface(Blueprint, Interface->GetClassPathName(), false);
        }
        return true;
    }
    if (Verb != TEXT("bp_interface_add"))
    {
        Error = TEXT("unknown interface verb: ") + Verb;
        return false;
    }
    if (!bImplemented && !FBlueprintEditorUtils::ImplementNewInterface(Blueprint, Interface->GetClassPathName()))
    {
        Error = TEXT("interface implementation conflicts with an existing member: ") + Path;
        return false;
    }
    return true;
}

static UEdGraph* DispatcherGraph(UBlueprint* Blueprint, FName Name)
{
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph && Graph->GetFName() == Name)
        {
            return Graph;
        }
    }
    return nullptr;
}

static UEdGraph* CreateDispatcher(UBlueprint* Blueprint, FName Name, FString& Error)
{
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
    if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type))
    {
        Error = TEXT("cannot declare dispatcher: ") + Name.ToString();
        return nullptr;
    }
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    Graph->bEditable = false;
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    Schema->CreateDefaultNodesForGraph(*Graph);
    Schema->CreateFunctionGraphTerminators(*Graph, static_cast<UClass*>(nullptr));
    Schema->AddExtraFunctionFlags(Graph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
    Schema->MarkFunctionEntryAsEditable(Graph, true);
    Blueprint->DelegateSignatureGraphs.Add(Graph);
    return Graph;
}

bool ApplyDispatcherVerb(UBlueprint* Blueprint, const FString& Verb, const TSharedPtr<FJsonObject>& Op, FString& Error)
{
    const FName Name(*ReadOpString(Op, TEXT("name")));
    UEdGraph* Graph = DispatcherGraph(Blueprint, Name);
    if (Verb == TEXT("bp_dispatcher_remove"))
    {
        if (Graph)
        {
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
            FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::Recompile);
        }
        return true;
    }
    const TSharedPtr<FJsonObject>* Signature = nullptr;
    if (!Op->TryGetObjectField(TEXT("signature"), Signature) || Signature == nullptr)
    {
        Error = TEXT("dispatcher signature is required");
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Outputs = nullptr;
    if ((*Signature)->TryGetArrayField(TEXT("outputs"), Outputs) && !Outputs->IsEmpty())
    {
        Error = TEXT("dispatcher signatures cannot return values");
        return false;
    }
    if (Verb == TEXT("bp_dispatcher_add") && Graph == nullptr)
    {
        Graph = CreateDispatcher(Blueprint, Name, Error);
    }
    if (Graph == nullptr)
    {
        Error = TEXT("dispatcher not found: ") + Name.ToString();
        return false;
    }
    if (Verb != TEXT("bp_dispatcher_add") && Verb != TEXT("bp_dispatcher_signature_set"))
    {
        Error = TEXT("unknown dispatcher verb: ") + Verb;
        return false;
    }
    const auto Effective = MakeShared<FJsonObject>(**Signature);
    TArray<TSharedPtr<FJsonValue>> Flags;
    Flags.Add(MakeShared<FJsonValueString>(TEXT("Public")));
    Effective->SetArrayField(TEXT("flags"), Flags);
    return ApplyBlueprintFunctionSignature(Graph, Effective, Error);
}
}
