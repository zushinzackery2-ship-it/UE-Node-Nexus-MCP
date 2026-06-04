#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"
#include "UeNodeNexusBridgeBlueprintPatchHelpers.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

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

static bool TryGetDefaultsObject(const TSharedPtr<FJsonObject>& Op, const TSharedPtr<FJsonObject>*& OutDefaults)
{
    if (Op->TryGetObjectField(TEXT("defaults"), OutDefaults) && OutDefaults != nullptr)
    {
        return true;
    }
    return Op->TryGetObjectField(TEXT("properties"), OutDefaults) && OutDefaults != nullptr;
}

static bool ReadVectorObject(const TSharedPtr<FJsonObject>& Object, FVector& OutValue)
{
    if (!Object.IsValid())
    {
        return false;
    }
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    if (!Object->TryGetNumberField(TEXT("x"), X))
    {
        Object->TryGetNumberField(TEXT("X"), X);
    }
    if (!Object->TryGetNumberField(TEXT("y"), Y))
    {
        Object->TryGetNumberField(TEXT("Y"), Y);
    }
    if (!Object->TryGetNumberField(TEXT("z"), Z))
    {
        Object->TryGetNumberField(TEXT("Z"), Z);
    }
    OutValue = FVector(X, Y, Z);
    return true;
}

static bool ReadRotatorObject(const TSharedPtr<FJsonObject>& Object, FRotator& OutValue)
{
    if (!Object.IsValid())
    {
        return false;
    }
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;
    if (!Object->TryGetNumberField(TEXT("pitch"), Pitch))
    {
        Object->TryGetNumberField(TEXT("Pitch"), Pitch);
    }
    if (!Object->TryGetNumberField(TEXT("yaw"), Yaw))
    {
        Object->TryGetNumberField(TEXT("Yaw"), Yaw);
    }
    if (!Object->TryGetNumberField(TEXT("roll"), Roll))
    {
        Object->TryGetNumberField(TEXT("Roll"), Roll);
    }
    OutValue = FRotator(Pitch, Yaw, Roll);
    return true;
}

static TSharedPtr<FJsonValueObject> MakeVectorValue(const FVector& Value)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("x"), Value.X);
    Object->SetNumberField(TEXT("y"), Value.Y);
    Object->SetNumberField(TEXT("z"), Value.Z);
    return MakeShared<FJsonValueObject>(Object);
}

static TSharedPtr<FJsonValueObject> MakeRotatorValue(const FRotator& Value)
{
    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
    Object->SetNumberField(TEXT("pitch"), Value.Pitch);
    Object->SetNumberField(TEXT("yaw"), Value.Yaw);
    Object->SetNumberField(TEXT("roll"), Value.Roll);
    return MakeShared<FJsonValueObject>(Object);
}

static bool ApplyTemplateProperty(UActorComponent* ComponentTemplate, const FName PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutValueText, FString& OutError)
{
    FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), PropertyName);
    if (Property == nullptr)
    {
        OutError = FString::Printf(TEXT("component_property_not_found: %s"), *PropertyName.ToString());
        return false;
    }
    if (!ApplyPropertyJsonValue(ComponentTemplate, Property, Value, OutValueText, OutError))
    {
        OutError = FString::Printf(TEXT("component_property_apply_failed: %s (%s)"), *PropertyName.ToString(), *OutError);
        return false;
    }
    return true;
}

static bool ApplyRelativeTransformDefault(USceneComponent* Component, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
    TSharedPtr<FJsonObject> TransformObject = Value.IsValid() ? Value->AsObject() : nullptr;
    if (!TransformObject.IsValid())
    {
        OutError = TEXT("relative_transform_must_be_object");
        return false;
    }

    const TSharedPtr<FJsonObject>* LocationObject = nullptr;
    if (TransformObject->TryGetObjectField(TEXT("location"), LocationObject) && LocationObject != nullptr)
    {
        FVector Location;
        FString ValueText;
        if (!ReadVectorObject(*LocationObject, Location) || !ApplyTemplateProperty(Component, TEXT("RelativeLocation"), MakeVectorValue(Location), ValueText, OutError))
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* RotationObject = nullptr;
    if (TransformObject->TryGetObjectField(TEXT("rotation"), RotationObject) && RotationObject != nullptr)
    {
        FRotator Rotation;
        FString ValueText;
        if (!ReadRotatorObject(*RotationObject, Rotation) || !ApplyTemplateProperty(Component, TEXT("RelativeRotation"), MakeRotatorValue(Rotation), ValueText, OutError))
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
    if (TransformObject->TryGetObjectField(TEXT("scale"), ScaleObject) && ScaleObject != nullptr)
    {
        FVector Scale;
        FString ValueText;
        if (!ReadVectorObject(*ScaleObject, Scale) || !ApplyTemplateProperty(Component, TEXT("RelativeScale3D"), MakeVectorValue(Scale), ValueText, OutError))
        {
            return false;
        }
    }
    return true;
}

static bool ApplyMaterialDefault(UMeshComponent* MeshComponent, const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>& Defaults, FString& OutError)
{
    FString MaterialPath;
    if (!Value.IsValid() || !Value->TryGetString(MaterialPath))
    {
        OutError = TEXT("material_value_must_be_path_string");
        return false;
    }
    UMaterialInterface* Material = MaterialPath.IsEmpty() || MaterialPath.Equals(TEXT("None"), ESearchCase::IgnoreCase) ? nullptr : LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (Material == nullptr && !MaterialPath.IsEmpty() && !MaterialPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("material_not_found");
        return false;
    }
    int32 SlotIndex = 0;
    Defaults->TryGetNumberField(TEXT("material_slot"), SlotIndex);
    MeshComponent->SetMaterial(SlotIndex, Material);
    return true;
}

static bool ApplyComponentDefaults(UActorComponent* ComponentTemplate, const TSharedPtr<FJsonObject>& Defaults, bool bDryRun, TSharedPtr<FJsonObject> Diff, const FString& ComponentName, FString& OutError)
{
    if (!Defaults.IsValid())
    {
        return true;
    }
    if (ComponentTemplate == nullptr && !bDryRun)
    {
        OutError = TEXT("component_template_unavailable");
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Applied;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Defaults->Values)
    {
        const FString& PropertyName = Field.Key;
        if (PropertyName.Equals(TEXT("material_slot"), ESearchCase::IgnoreCase))
        {
            continue;
        }

        if (bDryRun)
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("component"), ComponentName);
            Item->SetStringField(TEXT("property"), PropertyName);
            AppendDiffItem(Diff, TEXT("component_defaults_set"), Item);
            continue;
        }

        FString ValueText;
        if (PropertyName.Equals(TEXT("RelativeTransform"), ESearchCase::IgnoreCase) || PropertyName.Equals(TEXT("relative_transform"), ESearchCase::IgnoreCase))
        {
            USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentTemplate);
            if (SceneComponent == nullptr || !ApplyRelativeTransformDefault(SceneComponent, Field.Value, OutError))
            {
                return false;
            }
            ValueText = TEXT("RelativeTransform");
        }
        else if (PropertyName.Equals(TEXT("Material"), ESearchCase::IgnoreCase) || PropertyName.Equals(TEXT("material"), ESearchCase::IgnoreCase))
        {
            UMeshComponent* MeshComponent = Cast<UMeshComponent>(ComponentTemplate);
            if (MeshComponent == nullptr || !ApplyMaterialDefault(MeshComponent, Field.Value, Defaults, OutError))
            {
                return false;
            }
            ValueText = TEXT("Material");
        }
        else
        {
            if (!ApplyTemplateProperty(ComponentTemplate, FName(*PropertyName), Field.Value, ValueText, OutError))
            {
                return false;
            }
        }

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("component"), ComponentName);
        Item->SetStringField(TEXT("property"), PropertyName);
        Item->SetStringField(TEXT("value"), ValueText);
        Applied.Add(MakeShared<FJsonValueObject>(Item));
        ComponentTemplate->Modify();
        ComponentTemplate->PostEditChange();
    }

    for (const TSharedPtr<FJsonValue>& Item : Applied)
    {
        AppendDiffItem(Diff, TEXT("component_defaults_set"), Item->AsObject());
    }
    return true;
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
        const TSharedPtr<FJsonObject>* Defaults = nullptr;
        if (TryGetDefaultsObject(Op, Defaults))
        {
            ApplyComponentDefaults(nullptr, *Defaults, true, Diff, Name, OutError);
        }
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
    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if (TryGetDefaultsObject(Op, Defaults) && !ApplyComponentDefaults(NewNode->ComponentTemplate, *Defaults, false, Diff, NewNode->GetVariableName().ToString(), OutError))
    {
        Script->RemoveNodeAndPromoteChildren(NewNode);
        return false;
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

static bool SetComponentDefaults(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, FString& OutError)
{
    FString Name;
    USimpleConstructionScript* Script = Blueprint ? Blueprint->SimpleConstructionScript : nullptr;
    USCS_Node* Node = Op->TryGetStringField(TEXT("name"), Name) ? FindComponentNode(Script, Name) : nullptr;
    if (Node == nullptr)
    {
        OutError = FString::Printf(TEXT("component_not_found: %s"), *Name);
        return false;
    }

    const TSharedPtr<FJsonObject>* Defaults = nullptr;
    if (!TryGetDefaultsObject(Op, Defaults))
    {
        OutError = TEXT("defaults object is required for set_component_defaults");
        return false;
    }

    return ApplyComponentDefaults(Node->ComponentTemplate, *Defaults, bDryRun, Diff, Name, OutError);
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
