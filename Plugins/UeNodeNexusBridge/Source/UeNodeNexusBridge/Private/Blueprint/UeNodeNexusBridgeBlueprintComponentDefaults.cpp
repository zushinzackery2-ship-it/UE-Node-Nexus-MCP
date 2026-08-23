#include "UeNodeNexusBridgeBlueprintComponentDefaults.h"

#include "Components/ActorComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeGraphPatchShared.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
bool TryGetComponentDefaultsObject(
    const TSharedPtr<FJsonObject>& Operation,
    const TSharedPtr<FJsonObject>*& OutDefaults)
{
    if (Operation->TryGetObjectField(TEXT("defaults"), OutDefaults) && OutDefaults != nullptr)
    {
        return true;
    }
    return Operation->TryGetObjectField(TEXT("properties"), OutDefaults) && OutDefaults != nullptr;
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

static bool ApplyTemplateProperty(
    UActorComponent* ComponentTemplate,
    const FName PropertyName,
    const TSharedPtr<FJsonValue>& Value,
    FString& OutValueText,
    FString& OutError)
{
    FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), PropertyName);
    if (Property == nullptr)
    {
        OutError = FString::Printf(TEXT("component_property_not_found: %s"), *PropertyName.ToString());
        return false;
    }
    if (!ApplyPropertyJsonValue(ComponentTemplate, Property, Value, OutValueText, OutError))
    {
        OutError = FString::Printf(
            TEXT("component_property_apply_failed: %s (%s)"),
            *PropertyName.ToString(),
            *OutError);
        return false;
    }
    return true;
}

static bool ApplyRelativeTransformDefault(
    USceneComponent* Component,
    const TSharedPtr<FJsonValue>& Value,
    FString& OutError)
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
        if (!ReadVectorObject(*LocationObject, Location)
            || !ApplyTemplateProperty(
                Component,
                TEXT("RelativeLocation"),
                MakeVectorValue(Location),
                ValueText,
                OutError))
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* RotationObject = nullptr;
    if (TransformObject->TryGetObjectField(TEXT("rotation"), RotationObject) && RotationObject != nullptr)
    {
        FRotator Rotation;
        FString ValueText;
        if (!ReadRotatorObject(*RotationObject, Rotation)
            || !ApplyTemplateProperty(
                Component,
                TEXT("RelativeRotation"),
                MakeRotatorValue(Rotation),
                ValueText,
                OutError))
        {
            return false;
        }
    }

    const TSharedPtr<FJsonObject>* ScaleObject = nullptr;
    if (TransformObject->TryGetObjectField(TEXT("scale"), ScaleObject) && ScaleObject != nullptr)
    {
        FVector Scale;
        FString ValueText;
        if (!ReadVectorObject(*ScaleObject, Scale)
            || !ApplyTemplateProperty(
                Component,
                TEXT("RelativeScale3D"),
                MakeVectorValue(Scale),
                ValueText,
                OutError))
        {
            return false;
        }
    }
    return true;
}

static bool ApplyMaterialDefault(
    UMeshComponent* MeshComponent,
    const TSharedPtr<FJsonValue>& Value,
    const TSharedPtr<FJsonObject>& Defaults,
    FString& OutError)
{
    FString MaterialPath;
    if (!Value.IsValid() || !Value->TryGetString(MaterialPath))
    {
        OutError = TEXT("material_value_must_be_path_string");
        return false;
    }
    UMaterialInterface* Material =
        MaterialPath.IsEmpty() || MaterialPath.Equals(TEXT("None"), ESearchCase::IgnoreCase)
        ? nullptr
        : LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
    if (Material == nullptr
        && !MaterialPath.IsEmpty()
        && !MaterialPath.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        OutError = TEXT("material_not_found");
        return false;
    }
    int32 SlotIndex = 0;
    Defaults->TryGetNumberField(TEXT("material_slot"), SlotIndex);
    MeshComponent->SetMaterial(SlotIndex, Material);
    return true;
}

bool ApplyBlueprintComponentDefaults(
    UActorComponent* ComponentTemplate,
    const TSharedPtr<FJsonObject>& Defaults,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    const FString& ComponentName,
    FString& OutError)
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
        if (PropertyName.Equals(TEXT("RelativeTransform"), ESearchCase::IgnoreCase)
            || PropertyName.Equals(TEXT("relative_transform"), ESearchCase::IgnoreCase))
        {
            USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentTemplate);
            if (SceneComponent == nullptr
                || !ApplyRelativeTransformDefault(SceneComponent, Field.Value, OutError))
            {
                return false;
            }
            ValueText = TEXT("RelativeTransform");
        }
        else if (PropertyName.Equals(TEXT("Material"), ESearchCase::IgnoreCase)
            || PropertyName.Equals(TEXT("material"), ESearchCase::IgnoreCase))
        {
            UMeshComponent* MeshComponent = Cast<UMeshComponent>(ComponentTemplate);
            if (MeshComponent == nullptr
                || !ApplyMaterialDefault(MeshComponent, Field.Value, Defaults, OutError))
            {
                return false;
            }
            ValueText = TEXT("Material");
        }
        else if (!ApplyTemplateProperty(
            ComponentTemplate,
            FName(*PropertyName),
            Field.Value,
            ValueText,
            OutError))
        {
            return false;
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
}
