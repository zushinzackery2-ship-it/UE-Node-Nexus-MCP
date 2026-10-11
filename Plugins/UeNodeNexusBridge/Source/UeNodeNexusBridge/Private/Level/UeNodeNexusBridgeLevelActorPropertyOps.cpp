#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Properties/NexusActorProperties.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
namespace
{
UObject* ResolveWriteTarget(AActor* Actor, const TSharedPtr<FJsonObject>& Payload, FString& Error)
{
    FString Name;
    if (!Payload->TryGetStringField(TEXT("component"), Name) || Name.IsEmpty())
    {
        return Actor;
    }
    TInlineComponentArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        if (IsValid(Component) && Component->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }
    Error = FString::Printf(TEXT("component %s not found on actor"), *Name);
    return nullptr;
}

TSharedPtr<FJsonObject> WriteItem(AActor* Actor, const TSharedPtr<FJsonObject>& Payload,
    const FString& Name, const TSharedPtr<FJsonValue>& Value, bool bDryRun, bool& Changed)
{
    auto Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("name"), Name);
    FString Error;
    UObject* Target = ResolveWriteTarget(Actor, Payload, Error);
    ActorProperties::FPath Path;
    if (!Target || !ActorProperties::Resolve(Target, Name, Path, Error))
    {
        Item->SetBoolField(TEXT("ok"), false);
        Item->SetStringField(TEXT("error"), Error);
        return Item;
    }
    const FString Before = ActorProperties::Export(Target, Path);
    Item->SetStringField(TEXT("before"), Before);
    if (bDryRun)
    {
        return Item;
    }
    Target->Modify();
    Target->PreEditChange(Path.Root);
    bool bOk = ActorProperties::Write(Target, Path, Value, Error);
    FPropertyChangedEvent Event(Path.Leaf, EPropertyChangeType::ValueSet);
    Event.MemberProperty = Path.Root;
    Target->PostEditChangeProperty(Event);

    // Construction scripts can replace the component and every captured struct
    // container. Resolve both the identity and property path after the callback.
    FString RebindError;
    Target = ResolveWriteTarget(Actor, Payload, RebindError);
    if (!Target || !ActorProperties::Resolve(Target, Name, Path, RebindError))
    {
        bOk = false;
        Error = TEXT("target_changed_after_edit:") + RebindError;
    }
    else
    {
        const FString After = ActorProperties::Export(Target, Path);
        Item->SetStringField(TEXT("after"), After);
        Changed |= Before != After;
        if (Before != After)
        {
            Target->MarkPackageDirty();
        }
    }
    Item->SetBoolField(TEXT("ok"), bOk);
    if (!bOk)
    {
        Item->SetStringField(TEXT("error"), Error);
    }
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
    if (!IsValid(Actor))
    {
        return MakeOperationError(Operation, RequestId, TEXT("actor_not_found"), TEXT("Actor could not be resolved"));
    }
    FString TargetError;
    UObject* Target = ResolveWriteTarget(Actor, Payload, TargetError);
    if (!Target)
    {
        return MakeOperationError(Operation, RequestId, TEXT("component_not_found"), TargetError);
    }
    const FString TargetPath = Target->GetPathName();
    const TSharedPtr<FJsonObject>* Properties = nullptr;
    if (!Payload->TryGetObjectField(TEXT("properties"), Properties) || !Properties || !Properties->IsValid() || (*Properties)->Values.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("properties must be a non-empty object of dotted property paths"));
    }
    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    TArray<TSharedPtr<FJsonValue>> Invalid;
    for (const auto& Pair : (*Properties)->Values)
    {
        ActorProperties::FPath Path;
        FString Error;
        if (!ActorProperties::Resolve(Target, Pair.Key, Path, Error))
        {
            auto Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), Pair.Key);
            Item->SetStringField(TEXT("error"), Error);
            Invalid.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    if (!Invalid.IsEmpty())
    {
        auto Details = MakeShared<FJsonObject>();
        Details->SetStringField(TEXT("target_path"), TargetPath);
        Details->SetArrayField(TEXT("invalid"), Invalid);
        return MakeOperationError(Operation, RequestId, TEXT("invalid_property"), TEXT("one or more property paths did not resolve to an editable property"), Details);
    }
    // Only names and values survive preflight. Never retain object/container
    // pointers across a PostEditChange callback or between batch entries.
    TArray<TSharedPtr<FJsonValue>> Items;
    bool bChanged = false;
    int32 FailedCount = 0;
    for (const auto& Pair : (*Properties)->Values)
    {
        auto Item = WriteItem(Actor, Payload, Pair.Key, Pair.Value, bDryRun, bChanged);
        bool bOk = true;
        Item->TryGetBoolField(TEXT("ok"), bOk);
        FailedCount += bOk ? 0 : 1;
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    if (!bDryRun && bChanged)
    {
        Actor->MarkPackageDirty();
    }
    auto Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Data->SetStringField(TEXT("class_path"), Actor->GetClass()->GetPathName());
    Data->SetStringField(TEXT("target_path"), TargetPath);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetBoolField(TEXT("partial"), !bDryRun && bChanged && FailedCount > 0);
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetNumberField(TEXT("failed_count"), FailedCount);
    auto Response = MakeEnvelope(Operation, RequestId, FailedCount == 0);
    if (FailedCount > 0)
    {
        const FString Message = FString::Printf(TEXT("%d of %d properties failed to write or read back"), FailedCount, Items.Num());
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("property_write_failed")), Message));
    }
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
