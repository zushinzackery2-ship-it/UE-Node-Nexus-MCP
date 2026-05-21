#include "UeNodeNexusBridgeOperations.h"

#include "Components/MeshComponent.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"

namespace UeNodeNexusBridge
{
static void AddUsageItem(TArray<TSharedPtr<FJsonValue>>& Items, AActor* Actor, UMeshComponent* Component, int32 SlotIndex, UMaterialInterface* Material)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Item->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Item->SetStringField(TEXT("component_path"), Component->GetPathName());
    Item->SetNumberField(TEXT("slot_index"), SlotIndex);
    Item->SetStringField(TEXT("material_path"), Material ? Material->GetPathName() : FString());
    Item->SetStringField(TEXT("root_material_path"), Material && Material->GetMaterial() ? Material->GetMaterial()->GetPathName() : FString());
    Items.Add(MakeShared<FJsonValueObject>(Item));
}

static void AddUsageLine(FString& Text, int32 Index, AActor* Actor, UMeshComponent* Component, int32 SlotIndex, UMaterialInterface* Material)
{
    Text += FString::Printf(
        TEXT("U:%d:%s;slot=%d;actor=%s;mat=%s;root=%s\n"),
        Index,
        *EscapeIndexedToken(Component->GetPathName()),
        SlotIndex,
        *EscapeIndexedToken(Actor->GetActorLabel()),
        Material ? *EscapeIndexedToken(Material->GetPathName()) : TEXT(""),
        Material && Material->GetMaterial() ? *EscapeIndexedToken(Material->GetMaterial()->GetPathName()) : TEXT(""));
}

TSharedPtr<FJsonObject> HandleMaterialUsageFind(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    UMaterialInterface* Query = ResolveMaterialInterfaceFromPayload(Payload);
    if (Query == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("material_not_found"), TEXT("asset_path or material_path must resolve to a material interface"));
    }

    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    if (ActorSubsystem == nullptr)
    {
        return MakeMaterialInterfaceError(Operation, RequestId, TEXT("editor_actor_subsystem_unavailable"), TEXT("UEditorActorSubsystem is not available"));
    }

    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 100, 1000);
    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);

    int32 Matched = 0;
    int32 Returned = 0;
    bool bHasMore = false;
    FString Text;
    TArray<TSharedPtr<FJsonValue>> Items;
    for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
    {
        if (Actor == nullptr)
        {
            continue;
        }
        TInlineComponentArray<UMeshComponent*> Components;
        Actor->GetComponents(Components);
        for (UMeshComponent* Component : Components)
        {
            for (int32 SlotIndex = 0; Component != nullptr && SlotIndex < Component->GetNumMaterials(); ++SlotIndex)
            {
                UMaterialInterface* Material = Component->GetMaterial(SlotIndex);
                if (!MaterialMatchesQuery(Material, Query))
                {
                    continue;
                }
                if (Matched++ < Offset)
                {
                    continue;
                }
                if (Returned >= Limit)
                {
                    bHasMore = true;
                    continue;
                }
                bIndexed ? AddUsageLine(Text, Matched - 1, Actor, Component, SlotIndex, Material) : AddUsageItem(Items, Actor, Component, SlotIndex, Material);
                ++Returned;
            }
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("query_material_path"), Query->GetPathName());
    if (bIndexed)
    {
        Data->SetStringField(TEXT("format"), TEXT("material_usage_indexed"));
        SetTextPayload(Data, Text);
    }
    else
    {
        Data->SetArrayField(TEXT("items"), Items);
    }
    Data->SetNumberField(TEXT("count"), Returned);
    Data->SetBoolField(TEXT("has_more"), bHasMore);
    if (bHasMore)
    {
        Data->SetStringField(TEXT("next_cursor"), FString::FromInt(Offset + Returned));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
