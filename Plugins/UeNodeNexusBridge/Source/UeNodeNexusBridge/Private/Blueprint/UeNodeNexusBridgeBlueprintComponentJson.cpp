#include "UeNodeNexusBridgeBlueprintComponentJson.h"

#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
namespace
{
FString FirstComponentAsset(UActorComponent* Component)
{
    static const FName Names[] = {
        TEXT("StaticMesh"),
        TEXT("SkeletalMeshAsset"),
        TEXT("SkeletalMesh"),
        TEXT("AnimClass"),
        TEXT("Texture")
    };

    for (const FName& Name : Names)
    {
        FString Value;
        if (ExportNamedPropertyText(Component, Name, Value) && !Value.IsEmpty() && Value != TEXT("None"))
        {
            return Value;
        }
    }
    return FString();
}

TSharedPtr<FJsonValue> MakeComponentRow(const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket, const FString& Origin)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Name));
    Row.Add(MakeShared<FJsonValueString>(Component ? Component->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Parent));
    Row.Add(MakeShared<FJsonValueString>(Socket));
    Row.Add(MakeShared<FJsonValueString>(FirstComponentAsset(Component)));
    Row.Add(MakeShared<FJsonValueString>(Origin));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeComponentObject(const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket, const FString& Origin)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Name);
    Json->SetStringField(TEXT("class"), Component ? Component->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("parent"), Parent);
    Json->SetStringField(TEXT("socket"), Socket);
    Json->SetStringField(TEXT("asset"), FirstComponentAsset(Component));
    Json->SetStringField(TEXT("template_path"), Component ? Component->GetPathName() : FString());
    Json->SetStringField(TEXT("origin"), Origin);
    return Json;
}

void AddComponentEntry(TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames, bool bCompact, const FString& Name, UActorComponent* Component, const FString& Parent, const FString& Socket, const FString& Origin)
{
    if (Name.IsEmpty() || SeenNames.Contains(Name))
    {
        return;
    }

    SeenNames.Add(Name);
    Components.Add(bCompact ? MakeComponentRow(Name, Component, Parent, Socket, Origin) : MakeShared<FJsonValueObject>(MakeComponentObject(Name, Component, Parent, Socket, Origin)));
}

void AddSCSComponents(UBlueprintGeneratedClass* GeneratedClass, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames, const FString& Origin)
{
    USimpleConstructionScript* Script = GeneratedClass ? GeneratedClass->SimpleConstructionScript.Get() : nullptr;
    if (Script == nullptr)
    {
        return;
    }

    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node == nullptr)
        {
            continue;
        }

        UActorComponent* Component = Node->GetActualComponentTemplate(GeneratedClass);
        USCS_Node* ParentNode = Script->FindParentNode(Node);
        const FString Parent = ParentNode ? ParentNode->GetVariableName().ToString() : Node->ParentComponentOrVariableName.ToString();
        AddComponentEntry(Components, SeenNames, bCompact, Node->GetVariableName().ToString(), Component, Parent, Node->AttachToName.ToString(), Origin);
    }
}

// Walk the parent class chain so SCS components defined on ancestor Blueprints
// surface with their structural attach/socket info and an originating-class
// origin. USimpleConstructionScript::GetAllNodes returns only the nodes declared
// directly on its own Blueprint, so inherited component structure is otherwise lost.
void AddInheritedSCSComponents(UBlueprintGeneratedClass* GeneratedClass, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames)
{
    UClass* Super = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
    while (Super != nullptr)
    {
        if (UBlueprintGeneratedClass* SuperGeneratedClass = Cast<UBlueprintGeneratedClass>(Super))
        {
            AddSCSComponents(SuperGeneratedClass, bCompact, Components, SeenNames, SuperGeneratedClass->GetPathName());
        }
        Super = Super->GetSuperClass();
    }
}

void AddCDOComponentProperties(UBlueprintGeneratedClass* GeneratedClass, UObject* CDO, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Components, TSet<FString>& SeenNames)
{
    if (GeneratedClass == nullptr || CDO == nullptr)
    {
        return;
    }

    for (TFieldIterator<FObjectProperty> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectProperty* Property = *It;
        if (Property == nullptr || Property->PropertyClass == nullptr || !Property->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
        {
            continue;
        }

        UActorComponent* Component = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(CDO));
        AddComponentEntry(Components, SeenNames, bCompact, Property->GetName(), Component, FString(), FString(), FString());
    }
}
}

void CollectBlueprintComponents(UBlueprintGeneratedClass* GeneratedClass, UObject* CDO, bool bCompact, bool bIncludeInherited, TArray<TSharedPtr<FJsonValue>>& OutComponents)
{
    TSet<FString> SeenNames;
    AddSCSComponents(GeneratedClass, bCompact, OutComponents, SeenNames, FString());
    if (bIncludeInherited)
    {
        AddInheritedSCSComponents(GeneratedClass, bCompact, OutComponents, SeenNames);
    }
    AddCDOComponentProperties(GeneratedClass, CDO, bCompact, OutComponents, SeenNames);
}
}
