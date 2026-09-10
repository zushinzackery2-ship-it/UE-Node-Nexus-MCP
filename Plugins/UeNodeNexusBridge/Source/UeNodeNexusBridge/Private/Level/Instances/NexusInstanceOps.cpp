#include "NexusInstanceEdit.h"
#include "NexusInstanceIdentity.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Level/Scene/NexusSceneOps.h"
#include "Level/Scene/NexusSceneApply.h"
#include "ScopedTransaction.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge
{
using namespace Scene;

static UInstancedStaticMeshComponent* Target(const FObject& Payload)
{
    UInstancedStaticMeshComponent* Component = FindObject<UInstancedStaticMeshComponent>(nullptr, *String(Payload, TEXT("component_path")));
    return Component && SupportedActor(Component->GetOwner()) && !Component->IsTemplate()
        && GEditor && !GEditor->PlayWorld && Component->GetWorld() == GEditor->GetEditorWorldContext().World() ? Component : nullptr;
}

TSharedPtr<FJsonObject> HandleComponentInstancesGet(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    UInstancedStaticMeshComponent* Component = Target(Payload);
    FString Error;
    int32 Offset = 0;
    int32 Limit = 1000;
    Payload->TryGetNumberField(TEXT("offset"), Offset);
    Payload->TryGetNumberField(TEXT("limit"), Limit);
    if (Offset < 0 || Limit < 1 || Limit > 10000)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_pagination"), TEXT("offset >= 0 and limit 1..10000 are required"));
    }
    FObject Data;
    if (!Component || !Instances::Export(Component, Data, Error, false, Offset, Limit))
    {
        return MakeOperationError(Operation, RequestId, TEXT("instances_unavailable"), Error);
    }
    const FString Expected = String(Payload, TEXT("revision"));
    if (!Expected.IsEmpty() && Expected != String(Data, TEXT("revision")))
    {
        return MakeOperationError(Operation, RequestId, TEXT("instance_conflict"), TEXT("component changed between pages"));
    }
    Data->SetStringField(TEXT("component_path"), Component->GetPathName());
    FObject Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool BuildDesired(const FObject& Payload, const FObject& Current, FRows& Desired, FRows& AddedIds, FString& Error)
{
    TMap<FString, FObject> ById;
    TArray<FString> Order;
    for (const auto& Value : Rows(Current, TEXT("instances")))
    {
        FObject Item = MakeShared<FJsonObject>(*Value->AsObject());
        const FString Id = String(Item, TEXT("id"));
        ById.Add(Id, Item);
        Order.Add(Id);
    }
    for (const auto& Value : Rows(Payload, TEXT("ops")))
    {
        const FObject* Op = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(Op))
        {
            Error = TEXT("invalid_instance_operation");
            return false;
        }
        const FString Verb = String(*Op, TEXT("op"));
        FString Id = String(*Op, TEXT("id"));
        if (Verb == TEXT("add"))
        {
            if (Id.IsEmpty())
            {
                Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            }
            if (ById.Contains(Id))
            {
                Error = TEXT("duplicate_instance_id");
                return false;
            }
            FObject Item = MakeShared<FJsonObject>(**Op);
            Item->SetStringField(TEXT("id"), Id);
            ById.Add(Id, Item);
            Order.Add(Id);
            AddedIds.Add(MakeShared<FJsonValueString>(Id));
        }
        else if (!ById.Contains(Id))
        {
            Error = TEXT("unknown_instance_id: ") + Id;
            return false;
        }
        else if (Verb == TEXT("remove"))
        {
            ById.Remove(Id);
        }
        else if (Verb == TEXT("update"))
        {
            static const TCHAR* Keys[] =
            {
                TEXT("transform"), TEXT("custom_data")
            };
            for (const TCHAR* Key : Keys)
            {
                if ((*Op)->HasField(Key))
                {
                    ById[Id]->SetField(Key, (*Op)->Values[Key]);
                }
            }
        }
        else
        {
            Error = TEXT("unknown_instance_operation");
            return false;
        }
    }
    for (const FString& Id : Order)
    {
        if (ById.Contains(Id))
        {
            Desired.Add(MakeShared<FJsonValueObject>(ById[Id]));
        }
    }
    return true;
}

TSharedPtr<FJsonObject> HandleComponentInstancesPatch(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    const FRows* Operations = nullptr;
    if (!Payload->TryGetArrayField(TEXT("ops"), Operations))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_instance_patch"), TEXT("ops must be an array"));
    }
    UInstancedStaticMeshComponent* Component = Target(Payload);
    FObject Current;
    FString Error;
    if (!Component || Instances::IsConstructionOwned(Component) || !Instances::Export(Component, Current, Error))
    {
        return MakeOperationError(Operation, RequestId, TEXT("instances_unavailable"), Error);
    }
    if (String(Payload, TEXT("revision")) != String(Current, TEXT("revision")))
    {
        return MakeOperationError(Operation, RequestId, TEXT("instance_conflict"), TEXT("revision from component_instances_get is required"));
    }
    FRows Desired, Added;
    int32 Count = Component->NumCustomDataFloats;
    Payload->TryGetNumberField(TEXT("custom_data_count"), Count);
    TArray<Instances::FInstance> Parsed;
    if (!BuildDesired(Payload, Current, Desired, Added, Error) || !Instances::Parse(Desired, Count, Parsed, Error))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_instance_patch"), Error);
    }
    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);
    bool bOk = true;
    bool bApplied = false;
    FRows Saved, Failed;
    if (!bDryRun)
    {
        FApply Context;
        Context.World = Component->GetWorld();
        Context.Plan = MakeShared<FJsonObject>();
        AActor* Actor = Component->GetOwner();
        Context.Actors.Add(Actor->GetActorGuid().ToString(EGuidFormats::Digits), Actor);
        if (!PrepareResources(Context, false))
        {
            return MakeOperationError(Operation, RequestId, TEXT("instance_resources_not_ready"), Context.Error);
        }
        {
            FScopedTransaction Transaction(FText::FromString(TEXT("Nexus instance patch")));
            bOk = Instances::Apply(Component, Parsed, Count, Error);
            bApplied = bOk;
        }
        if (bOk)
        {
            FinishRenderingUpdates();
            if (bSave)
            {
                Context.Touch(Actor);
                bOk = SaveScene(Context, Saved, Failed);
                if (!bOk)
                {
                    Error = TEXT("instance_save_failed: inspect failed_packages");
                }
            }
        }
    }
    FObject Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApplied);
    Data->SetBoolField(TEXT("saved"), !bDryRun && bSave && bOk);
    Data->SetArrayField(TEXT("saved_packages"), Saved);
    Data->SetArrayField(TEXT("failed_packages"), Failed);
    Data->SetArrayField(TEXT("added_ids"), Added);
    Data->SetNumberField(TEXT("instance_count"), Component->GetInstanceCount());
    Data->SetNumberField(TEXT("planned_instance_count"), Parsed.Num());
    FObject After;
    FString ReadError;
    if (Instances::Export(Component, After, ReadError))
    {
        Data->SetStringField(TEXT("revision"), String(After, TEXT("revision")));
    }
    FObject Response = MakeEnvelope(Operation, RequestId, bOk);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bOk)
    {
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("instance_patch_failed")), Error));
    }
    return Response;
}
}
