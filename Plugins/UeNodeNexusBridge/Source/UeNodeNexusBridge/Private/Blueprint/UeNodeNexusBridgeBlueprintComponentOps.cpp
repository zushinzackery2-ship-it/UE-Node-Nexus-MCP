#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintComponentDefaults.h"
#include "UeNodeNexusBridgeBlueprintComponentHelpers.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeBlueprintComponentError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    return MakeOperationError(Operation, RequestId, Code, Message);
}

static TSharedPtr<FJsonObject> MakeBlueprintComponentInvalidParams(const FString& Operation, const FString& RequestId)
{
    // Keep the actionable message + error code in step with the sibling patch ops
    // (graph/material/material-function/project-input all say "operations must be
    // an array" under invalid_request), and attach a complete field->type schema
    // covering every payload field HandleBlueprintComponentsPatch consumes.
    TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
    Details->SetStringField(TEXT("asset_path"), TEXT("string"));
    Details->SetStringField(TEXT("operations"), TEXT("array<object>"));
    Details->SetStringField(TEXT("dry_run"), TEXT("boolean"));
    Details->SetStringField(TEXT("compile_after"), TEXT("boolean"));
    Details->SetStringField(TEXT("format"), TEXT("string"));
    return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("operations must be an array"), Details);
}

static bool AddComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutError)
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
    if (Script == nullptr
        || (!Name.IsEmpty() && FindBlueprintSCSNode(Script, Name) != nullptr))
    {
        return false;
    }

    USCS_Node* ParentNode = FindBlueprintSCSNode(Script, ParentName);
    USceneComponent* NativeParent =
        ParentNode == nullptr
        ? FindNativeBlueprintSceneComponent(Blueprint, ParentName)
        : nullptr;
    if (!ParentName.IsEmpty() && ParentNode == nullptr && NativeParent == nullptr)
    {
        return false;
    }

    if (bDryRun)
    {
        const TSharedPtr<FJsonObject>* Defaults = nullptr;
        if (TryGetComponentDefaultsObject(Op, Defaults))
        {
            UActorComponent* Template = NewObject<UActorComponent>(GetTransientPackage(), ComponentClass);
            if (!ApplyBlueprintComponentDefaults(Template, *Defaults, true, Diff, Name, OutError))
            {
                return false;
            }
        }
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
    }
    else
    {
        if (NativeParent != nullptr)
        {
            NewNode->SetParent(NativeParent);
        }
        Script->AddNode(NewNode);
    }
    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if (TryGetComponentDefaultsObject(Op, Defaults)
        && !ApplyBlueprintComponentDefaults(
            NewNode->ComponentTemplate,
            *Defaults,
            false,
            Diff,
            NewNode->GetVariableName().ToString(),
            OutError))
    {
        Script->RemoveNodeAndPromoteChildren(NewNode);
        return false;
    }
    AppendDiffItem(
        Diff,
        TEXT("components_added"),
        MakeBlueprintComponentItem(NewNode, ParentName));
    return true;
}

static bool RemoveComponent(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff)
{
    FString Name;
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    USCS_Node* Node = Op->TryGetStringField(TEXT("name"), Name)
        ? FindBlueprintSCSNode(Script, Name)
        : nullptr;
    if (Node == nullptr)
    {
        return false;
    }
    USCS_Node* ParentNode = Script->FindParentNode(Node);
    const FString Parent = ParentNode ? ParentNode->GetVariableName().ToString() : Node->ParentComponentOrVariableName.ToString();
    AppendDiffItem(
        Diff,
        TEXT("components_removed"),
        MakeBlueprintComponentItem(Node, Parent));
    if (!bDryRun)
    {
        Script->RemoveNodeAndPromoteChildren(Node);
    }
    return true;
}

static bool SetComponentDefaults(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutError)
{
    FString Name;
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    USCS_Node* Node = Op->TryGetStringField(TEXT("name"), Name)
        ? FindBlueprintSCSNode(Script, Name)
        : nullptr;
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("component_not_found: %s"), *Name);
        return false;
    }

    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if (!TryGetComponentDefaultsObject(Op, Defaults))
    {
        OutError = TEXT("defaults object is required for set_component_defaults");
        return false;
    }

    return ApplyBlueprintComponentDefaults(
        Node->ComponentTemplate,
        *Defaults,
        bDryRun,
        Diff,
        Name,
        OutError);
}

static bool ApplyComponentOperation(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutError)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        return false;
    }
    if (OpName == TEXT("add_component"))
    {
        return AddComponent(Blueprint, Op, bDryRun, Diff, OutError);
    }
    if (OpName == TEXT("remove_component"))
    {
        return RemoveComponent(Blueprint, Op, bDryRun, Diff);
    }
    if (OpName == TEXT("set_component_defaults") || OpName == TEXT("set_component_properties"))
    {
        return SetComponentDefaults(Blueprint, Op, bDryRun, Diff, OutError);
    }
    OutError = FString::Printf(TEXT("unsupported component patch op: %s"), *OpName);
    return false;
}

TSharedPtr<FJsonObject> HandleBlueprintComponentsPatch(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    UBlueprint* Blueprint = Payload->TryGetStringField(TEXT("asset_path"), AssetPath) ? LoadObject<UBlueprint>(nullptr, *AssetPath) : nullptr;
    if (Blueprint == nullptr)
    {
        return MakeBlueprintComponentError(Operation, RequestId, TEXT("blueprint_not_found"), TEXT("Blueprint could not be loaded"));
    }
    if (Blueprint->SimpleConstructionScript == nullptr)
    {
        return MakeBlueprintComponentError(Operation, RequestId, TEXT("blueprint_scs_unavailable"), TEXT("Blueprint does not expose a SimpleConstructionScript for component patching"));
    }
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        return MakeBlueprintComponentInvalidParams(Operation, RequestId);
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
        FString OpError;
        if (!Op.IsValid() || !ApplyComponentOperation(Blueprint, Op, bDryRun, Diff, OpError))
        {
            const FString Message = OpError.IsEmpty() ? TEXT("Blueprint component patch operation failed validation or application") : OpError;
            Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(TEXT("error"), TEXT("component_patch_operation_failed"), Message, Blueprint->GetPathName(), TEXT("UeNodeNexusBridge"))));
            continue;
        }
        bChanged = true;
    }
    if (!bDryRun && bChanged)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }

    TSharedPtr<FJsonObject> Compile = MakeCompilePostCheck(bCompileAfter, false, bDryRun || !bCompileAfter, 0, 0);
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
