#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "K2Node_FunctionEntry.h"

namespace UeNodeNexusBridge::Transcode
{
static TSharedPtr<FJsonObject> ComponentJson(const FString& Name, const FString& Guid, UActorComponent* Template, UObject* Defaults, const FString& Parent, const FString& Socket, bool bInherited)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Name);
    Json->SetStringField(TEXT("guid"), Guid);
    Json->SetStringField(TEXT("class"), Template ? Template->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("class_short"), Template ? Template->GetClass()->GetName() : FString());
    Json->SetStringField(TEXT("parent"), Parent);
    Json->SetStringField(TEXT("socket"), Socket);
    Json->SetBoolField(TEXT("inherited"), bInherited);
    Json->SetArrayField(TEXT("props"), Template ? ExportEditableProps(Template, Defaults) : TArray<TSharedPtr<FJsonValue>>());
    return Json;
}

static TArray<TSharedPtr<FJsonValue>> ComponentsJson(UBlueprint* Blueprint, UBlueprintGeneratedClass* GeneratedClass, UObject* Cdo)
{
    TArray<TSharedPtr<FJsonValue>> Components;
    TSet<FString> Seen;
    USimpleConstructionScript* Scs = Blueprint->SimpleConstructionScript;
    if (Scs != nullptr)
    {
        for (USCS_Node* Node : Scs->GetAllNodes())
        {
            if (Node == nullptr)
            {
                continue;
            }
            const FString Name = Node->GetVariableName().ToString();
            USCS_Node* ParentNode = Scs->FindParentNode(Node);
            const FString Parent = ParentNode ? ParentNode->GetVariableName().ToString() : Node->ParentComponentOrVariableName.ToString();
            const FString Socket = Node->AttachToName.IsNone() ? FString() : Node->AttachToName.ToString();
            UActorComponent* Template = Node->GetActualComponentTemplate(GeneratedClass);
            Seen.Add(Name);
            Components.Add(MakeShared<FJsonValueObject>(ComponentJson(Name, Node->VariableGuid.ToString(EGuidFormats::DigitsWithHyphens), Template, nullptr, Parent == TEXT("None") ? FString() : Parent, Socket, false)));
        }
    }
    AActor* ActorCdo = Cast<AActor>(Cdo);
    if (ActorCdo != nullptr)
    {
        TInlineComponentArray<UActorComponent*> Natives;
        ActorCdo->GetComponents(Natives);
        for (UActorComponent* Component : Natives)
        {
            if (Component == nullptr || Seen.Contains(Component->GetName()))
            {
                continue;
            }
            Seen.Add(Component->GetName());
            Components.Add(MakeShared<FJsonValueObject>(ComponentJson(Component->GetName(), FString(), Component, Component->GetArchetype(), FString(), FString(), true)));
        }
    }
    return Components;
}

static TArray<TSharedPtr<FJsonValue>> DefaultsJson(UBlueprintGeneratedClass* GeneratedClass, UObject* Cdo, UClass* ParentClass)
{
    TArray<TSharedPtr<FJsonValue>> Defaults;
    if (Cdo == nullptr || ParentClass == nullptr)
    {
        return Defaults;
    }
    UObject* ParentCdo = ParentClass->GetDefaultObject();
    for (TFieldIterator<FProperty> It(ParentClass); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsEditableProperty(Property))
        {
            continue;
        }
        if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property); ObjectProperty && ObjectProperty->PropertyClass && ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Property->GetName());
        Json->SetStringField(TEXT("type"), Property->GetCPPType());
        Json->SetStringField(TEXT("value"), ExportPropertyValue(Cdo, Property));
        Json->SetStringField(TEXT("default"), ExportPropertyValue(ParentCdo, Property));
        Defaults.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Defaults;
}

static TArray<TSharedPtr<FJsonValue>> DispatchersJson(UBlueprint* Blueprint)
{
    TArray<TSharedPtr<FJsonValue>> Dispatchers;
    for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        TArray<UK2Node_FunctionEntry*> Entries;
        Graph->GetNodesOfClass(Entries);
        TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("name"), Graph->GetName());
        Json->SetArrayField(TEXT("params"), UserPinsJson(Entries.Num() > 0 ? Entries[0] : nullptr));
        Dispatchers.Add(MakeShared<FJsonValueObject>(Json));
    }
    return Dispatchers;
}

TSharedPtr<FJsonObject> BuildBlueprintRaw(UBlueprint* Blueprint)
{
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
    UObject* Cdo = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;

    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Blueprint, TEXT("blueprint"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Blueprint));

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
    TArray<TSharedPtr<FJsonValue>> Variables;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        Variables.Add(MakeShared<FJsonValueObject>(VariableJson(Variable, Cdo)));
    }
    Data->SetArrayField(TEXT("variables"), Variables);
    Data->SetArrayField(TEXT("components"), ComponentsJson(Blueprint, GeneratedClass, Cdo));
    Data->SetArrayField(TEXT("defaults"), DefaultsJson(GeneratedClass, Cdo, Blueprint->ParentClass));
    Data->SetArrayField(TEXT("dispatchers"), DispatchersJson(Blueprint));
    TArray<TSharedPtr<FJsonValue>> Interfaces;
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Interface != nullptr)
        {
            Interfaces.Add(MakeShared<FJsonValueString>(Interface.Interface->GetPathName()));
        }
    }
    Data->SetArrayField(TEXT("interfaces"), Interfaces);

    TArray<TSharedPtr<FJsonValue>> Graphs;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        if (Graph != nullptr)
        {
            Graphs.Add(MakeShared<FJsonValueObject>(GraphJson(Blueprint, Graph, TEXT("ubergraph"))));
        }
    }
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph != nullptr)
        {
            Graphs.Add(MakeShared<FJsonValueObject>(GraphJson(Blueprint, Graph, TEXT("function"))));
        }
    }
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph != nullptr)
        {
            Graphs.Add(MakeShared<FJsonValueObject>(GraphJson(Blueprint, Graph, TEXT("macro"))));
        }
    }
    Data->SetArrayField(TEXT("graphs"), Graphs);
    Raw->SetObjectField(TEXT("blueprint"), Data);
    return Raw;
}
}
