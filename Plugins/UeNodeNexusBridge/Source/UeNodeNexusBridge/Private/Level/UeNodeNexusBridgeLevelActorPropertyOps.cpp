#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

// level_actor_properties_set: write editable reflected properties on one placed
// actor (or one of its components) by dotted path, e.g. "bUnbound" or
// "Settings.AutoExposureBias" on a PostProcessVolume. Every path is validated
// before anything is written, so a typo cannot leave the actor half-edited.
// Scope is deliberately level actors and their components: assets go through
// the text mirror, and nothing here reaches CDOs or arbitrary UObjects.
namespace UeNodeNexusBridge
{
namespace
{
struct FResolvedPropertyPath
{
    FProperty* Leaf = nullptr;
    // Top-level property on the target; PreEditChange / PostEditChangeProperty
    // expect this one so components re-register the same way the details panel
    // makes them.
    FProperty* Root = nullptr;
    void* Container = nullptr;
};

bool ResolvePropertyPath(UObject* Target, const FString& Path, FResolvedPropertyPath& Out, FString& OutError)
{
    TArray<FString> Parts;
    Path.ParseIntoArray(Parts, TEXT("."), true);
    if (Parts.Num() == 0)
    {
        OutError = TEXT("empty_property_path");
        return false;
    }

    UStruct* Scope = Target->GetClass();
    void* Container = Target;
    for (int32 Index = 0; Index < Parts.Num(); ++Index)
    {
        FProperty* Property = Scope->FindPropertyByName(FName(*Parts[Index]));
        if (Property == nullptr)
        {
            OutError = FString::Printf(TEXT("unknown_property:%s"), *Parts[Index]);
            return false;
        }
        if (!ShouldExposeProperty(Property, false))
        {
            OutError = FString::Printf(TEXT("property_not_editable:%s"), *Parts[Index]);
            return false;
        }
        if (Property->ArrayDim != 1)
        {
            OutError = FString::Printf(TEXT("static_array_unsupported:%s"), *Parts[Index]);
            return false;
        }
        if (Index == 0)
        {
            Out.Root = Property;
        }
        if (Index == Parts.Num() - 1)
        {
            Out.Leaf = Property;
            Out.Container = Container;
            return true;
        }
        FStructProperty* StructProperty = CastField<FStructProperty>(Property);
        if (StructProperty == nullptr)
        {
            OutError = FString::Printf(TEXT("not_a_struct:%s"), *Parts[Index]);
            return false;
        }
        Container = StructProperty->ContainerPtrToValuePtr<void>(Container);
        Scope = StructProperty->Struct;
    }
    OutError = TEXT("unresolved_property_path");
    return false;
}

FString ExportLeafText(UObject* Owner, const FResolvedPropertyPath& Resolved)
{
    FString Text;
    const void* ValuePtr = Resolved.Leaf->ContainerPtrToValuePtr<void>(Resolved.Container);
    Resolved.Leaf->ExportText_Direct(Text, ValuePtr, ValuePtr, Owner, PPF_None);
    return Text;
}

bool ApplyLeafValue(UObject* Owner, const FResolvedPropertyPath& Resolved, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
    void* ValuePtr = Resolved.Leaf->ContainerPtrToValuePtr<void>(Resolved.Container);
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Resolved.Leaf))
    {
        FString Path;
        if (!Value.IsValid() || !Value->TryGetString(Path))
        {
            OutError = TEXT("object_property_value_must_be_path_string");
            return false;
        }
        const bool bNone = Path.IsEmpty() || Path.Equals(TEXT("None"), ESearchCase::IgnoreCase);
        UObject* NewValue = bNone ? nullptr : ResolveObjectByPath(Path);
        if (!bNone && NewValue == nullptr)
        {
            OutError = TEXT("object_property_value_not_found");
            return false;
        }
        if (NewValue != nullptr && ObjectProperty->PropertyClass != nullptr && !NewValue->IsA(ObjectProperty->PropertyClass))
        {
            OutError = TEXT("object_property_class_mismatch");
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(ValuePtr, NewValue);
        return true;
    }

    // A JSON string is taken verbatim as UE ExportText grammar, which covers enum
    // names, "(X=1,Y=2,Z=3)" structs and quoted strings alike; other JSON shapes
    // go through the shared vector/rotator/color/number/bool conversion.
    FString Text;
    if (!Value.IsValid() || !Value->TryGetString(Text))
    {
        if (!JsonValueToPropertyImportText(Resolved.Leaf, Value, Text, OutError))
        {
            return false;
        }
    }
    if (Resolved.Leaf->ImportText_Direct(*Text, ValuePtr, Owner, PPF_None) == nullptr)
    {
        OutError = FString::Printf(TEXT("import_failed:%s"), *Text);
        return false;
    }
    return true;
}

UObject* ResolveWriteTarget(AActor* Actor, const TSharedPtr<FJsonObject>& Payload, FString& OutError)
{
    FString ComponentName;
    if (!Payload->TryGetStringField(TEXT("component"), ComponentName) || ComponentName.IsEmpty())
    {
        return Actor;
    }
    TInlineComponentArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (Component != nullptr && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }
    OutError = FString::Printf(TEXT("component %s not found on actor"), *ComponentName);
    return nullptr;
}

TSharedPtr<FJsonObject> MakeItem(const FString& Name, const FString& Before)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    Item->SetStringField(TEXT("before"), Before);
    return Item;
}
}

TSharedPtr<FJsonObject> HandleLevelActorPropertiesSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString ActorPath;
    if (!Payload->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("actor_path is required"));
    }
    AActor* Actor = Cast<AActor>(ResolveObjectByPath(ActorPath));
    if (Actor == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("actor_not_found"), TEXT("Actor could not be resolved"));
    }

    FString TargetError;
    UObject* Target = ResolveWriteTarget(Actor, Payload, TargetError);
    if (Target == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("component_not_found"), TargetError);
    }

    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Payload->TryGetObjectField(TEXT("properties"), Properties) || Properties == nullptr || !Properties->IsValid() || (*Properties)->Values.Num() == 0)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("properties must be a non-empty object of dotted property paths"));
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    // Validate every path first so a single bad name fails the call before any
    // write; the details list tells the caller exactly which one.
    struct FPlanned
    {
        FString Name;
        FResolvedPropertyPath Resolved;
        TSharedPtr<FJsonValue> Value;
    };
    TArray<FPlanned> Planned;
    TArray<TSharedPtr<FJsonValue>> Invalid;
    for (const auto& Pair : (*Properties)->Values)
    {
        FPlanned Entry;
        Entry.Name = Pair.Key;
        Entry.Value = Pair.Value;
        FString Error;
        if (!ResolvePropertyPath(Target, Pair.Key, Entry.Resolved, Error))
        {
            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), Pair.Key);
            Item->SetStringField(TEXT("error"), Error);
            Invalid.Add(MakeShared<FJsonValueObject>(Item));
            continue;
        }
        Planned.Add(MoveTemp(Entry));
    }
    if (Invalid.Num() > 0)
    {
        TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("target_path"), Target->GetPathName());
        Details->SetArrayField(TEXT("invalid"), Invalid);
        return MakeOperationError(Operation, RequestId, TEXT("invalid_property"), TEXT("one or more property paths did not resolve to an editable property"), Details);
    }

    TArray<TSharedPtr<FJsonValue>> Items;
    bool bChanged = false;
    int32 FailedCount = 0;
    if (!bDryRun)
    {
        Target->Modify();
    }
    for (FPlanned& Entry : Planned)
    {
        const FString Before = ExportLeafText(Target, Entry.Resolved);
        TSharedPtr<FJsonObject> Item = MakeItem(Entry.Name, Before);
        if (!bDryRun)
        {
            Target->PreEditChange(Entry.Resolved.Root);
            FString Error;
            const bool bOk = ApplyLeafValue(Target, Entry.Resolved, Entry.Value, Error);
            FPropertyChangedEvent Event(Entry.Resolved.Leaf, EPropertyChangeType::ValueSet);
            Event.MemberProperty = Entry.Resolved.Root;
            Target->PostEditChangeProperty(Event);

            const FString After = ExportLeafText(Target, Entry.Resolved);
            Item->SetStringField(TEXT("after"), After);
            Item->SetBoolField(TEXT("ok"), bOk);
            if (!bOk)
            {
                Item->SetStringField(TEXT("error"), Error);
                ++FailedCount;
            }
            bChanged |= (Before != After);
        }
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (!bDryRun && bChanged)
    {
        Target->MarkPackageDirty();
        Actor->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("class_path"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString());
    Data->SetStringField(TEXT("target_path"), Target->GetPathName());
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetNumberField(TEXT("failed_count"), FailedCount);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, FailedCount == 0);
    if (FailedCount > 0)
    {
        // Qualified on purpose: with a TCHAR literal argument, ADL would pick the
        // engine's TValueOrError MakeError template over this namespace's helper.
        const FString Message = FString::Printf(TEXT("%d of %d properties failed to import"), FailedCount, Items.Num());
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("property_write_failed")), Message));
    }
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
