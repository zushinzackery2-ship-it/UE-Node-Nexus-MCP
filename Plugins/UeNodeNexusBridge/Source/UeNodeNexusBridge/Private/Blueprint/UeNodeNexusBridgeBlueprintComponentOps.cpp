#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeBlueprintComponentError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(Code, Message));
    return Response;
}

static USCS_Node* FindComponentNode(USimpleConstructionScript* Script, const FString& Name)
{
    if (Script == nullptr || Name.IsEmpty())
    {
        return nullptr;
    }
    if (USCS_Node* ExactNode = Script->FindSCSNode(FName(*Name)))
    {
        return ExactNode;
    }
    for (USCS_Node* Node : Script->GetAllNodes())
    {
        if (Node != nullptr && Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Node;
        }
    }
    return nullptr;
}

static USceneComponent* FindNativeSceneComponent(UBlueprint* Blueprint, const FString& Name)
{
    UBlueprintGeneratedClass* GeneratedClass = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get()) : nullptr;
    AActor* DefaultActor = GeneratedClass ? Cast<AActor>(GeneratedClass->GetDefaultObject()) : nullptr;
    if (DefaultActor == nullptr)
    {
        return nullptr;
    }
    if (Name.Equals(TEXT("RootComponent"), ESearchCase::IgnoreCase))
    {
        return DefaultActor->GetRootComponent();
    }
    for (TFieldIterator<FObjectProperty> It(GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectProperty* Property = *It;
        if (Property != nullptr && Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            if (USceneComponent* Component = Cast<USceneComponent>(Property->GetObjectPropertyValue_InContainer(DefaultActor)))
            {
                return Component;
            }
        }
    }
    TInlineComponentArray<USceneComponent*> Components(DefaultActor);
    for (USceneComponent* Component : Components)
    {
        if (Component != nullptr && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }
    return nullptr;
}

static TSharedPtr<FJsonObject> MakeComponentItem(USCS_Node* Node, const FString& ParentName)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Node ? Node->GetVariableName().ToString() : FString());
    Item->SetStringField(TEXT("component_class"), Node && Node->ComponentClass ? Node->ComponentClass->GetPathName() : FString());
    Item->SetStringField(TEXT("template_path"), Node && Node->ComponentTemplate ? Node->ComponentTemplate->GetPathName() : FString());
    Item->SetStringField(TEXT("parent"), ParentName);
    return Item;
}

static bool AddComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString ComponentClassPath;
    if (!Op->TryGetStringField(TEXT("component_class"), ComponentClassPath) || ComponentClassPath.IsEmpty())
    {
        return false;
    }
    UClass* ComponentClass = LoadClass<UActorComponent>(nullptr, *ComponentClassPath);
    if (ComponentClass == nullptr || ComponentClass->HasAnyClassFlags(CLASS_Abstract))
    {
        return false;
    }

    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    FString Name;
    FString ParentName;
    Op->TryGetStringField(TEXT("name"), Name);
    Op->TryGetStringField(TEXT("parent"), ParentName);
    if (Script == nullptr || (!Name.IsEmpty() && FindComponentNode(Script, Name) != nullptr))
    {
        return false;
    }

    USCS_Node* ParentNode = FindComponentNode(Script, ParentName);
    USceneComponent* NativeParent = ParentNode == nullptr ? FindNativeSceneComponent(Blueprint, ParentName) : nullptr;
    if (!ParentName.IsEmpty() && ParentNode == nullptr && NativeParent == nullptr)
    {
        return false;
    }

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Name);
        Item->SetStringField(TEXT("component_class"), ComponentClass->GetPathName());
        Item->SetStringField(TEXT("parent"), ParentName);
        AppendDiffItem(Diff, TEXT("components_added"), Item);
        return true;
    }

    USCS_Node* NewNode = Script->CreateNode(ComponentClass, Name.IsEmpty() ? NAME_None : FName(*Name));
    if (NewNode == nullptr)
    {
        return false;
    }
    if (ParentNode != nullptr)
    {
        ParentNode->AddChildNode(NewNode);
        NewNode->SetParent(ParentNode);
    }
    else
    {
        if (NativeParent != nullptr)
        {
            NewNode->SetParent(NativeParent);
        }
        Script->AddNode(NewNode);
    }
    AppendDiffItem(Diff, TEXT("components_added"), MakeComponentItem(NewNode, ParentName));
    return true;
}

static bool RemoveComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString Name;
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    USCS_Node* Node = Op->TryGetStringField(TEXT("name"), Name) ? FindComponentNode(Script, Name) : nullptr;
    if (Node == nullptr)
    {
        return false;
    }
    AppendDiffItem(Diff, TEXT("components_removed"), MakeComponentItem(Node, Node->ParentComponentOrVariableName.ToString()));
    if (!bDryRun)
    {
        Script->RemoveNodeAndPromoteChildren(Node);
    }
    return true;
}

static bool ApplyComponentOperation(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        return false;
    }
    if (OpName == TEXT("add_component"))
    {
        return AddComponent(Blueprint, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("remove_component"))
    {
        return RemoveComponent(Blueprint, Op, bDryRun, Diff);
    }
    return false;
}

TSharedPtr<FJsonObject> HandleBlueprintComponentsPatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    UBlueprint* Blueprint = Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ? LoadObject<UBlueprint>(nullptr, *AssetPath) : nullptr;
    if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
    {
        return MakeBlueprintComponentError(Operation, RequestId, TEXT("blueprint_not_found"), TEXT("Blueprint with a SimpleConstructionScript could not be loaded"));
    }
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        return MakeBlueprintComponentError(Operation, RequestId, TEXT("invalid_request"), TEXT("operations must be an array"));
    }

    bool bDryRun = true;
    bool bCompileAfter = true;
    FString Format = TEXT("compact");
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("compile_after"), bCompileAfter);
    Payload->TryGetStringField(TEXT("format"), Format);

    TSharedPtr<FJsonObject> Diff = MakeEmptyDiff();
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    bool bChanged = false;
    TUniquePtr<FScopedTransaction> Transaction;
    if (!bDryRun)
    {
        Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("UE Node Nexus Blueprint Components Patch")));
        Blueprint->Modify();
        Blueprint->SimpleConstructionScript->Modify();
    }
    for (const TSharedPtr<FJsonValue>& Value : *Operations)
    {
        TSharedPtr<FJsonObject> Op = Value->AsObject();
        if (!Op.IsValid() || !ApplyComponentOperation(Blueprint, Op, bDryRun, Diff))
        {
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("component_patch_operation_failed"), TEXT("Blueprint component patch operation failed validation or application"), Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }
    if (!bDryRun && bChanged)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, false, !bCompileAfter, 0, 0);
    if (!bDryRun && bCompileAfter)
    {
        Diagnostics.Append(CompileBlueprintWithDiagnostics(Blueprint, Blueprint->GetPathName(), Compile));
    }
    const bool bOk = Diagnostics.Num() == 0 && (!bCompileAfter || Compile->GetBoolField(TEXT("ok")));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), MakeWriteDataWithDiffFormat(bDryRun, !bDryRun && bChanged, bChanged, Diff, MakePinIntegrity(true, {}, {}), Compile, MakeDirtyState(Blueprint), Format));
    Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
    return Response;
}
}
