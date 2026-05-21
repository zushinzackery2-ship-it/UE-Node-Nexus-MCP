#include "UeNodeNexusBridgeOperations.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UeNodeNexusBridgeGraphIndexedInfoOps.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeLevelMaterialShared.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeInvalidLevelMaterialResponse(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(Code, Message));
    return Response;
}

UMeshComponent* ResolveMeshComponent(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    FString ComponentPath;
    if (!Payload->TryGetStringField(TEXT("component_path"), ComponentPath) || ComponentPath.IsEmpty())
    {
        OutResponse = MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("invalid_request"), TEXT("component_path is required"));
        return nullptr;
    }

    UMeshComponent* Component = Cast<UMeshComponent>(ResolveObjectByPath(ComponentPath));
    if (Component == nullptr)
    {
        OutResponse = MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("component_not_found"), TEXT("Mesh component could not be resolved"));
        return nullptr;
    }
    return Component;
}

static FString MaterialRootPath(UMaterialInterface* MaterialInterface)
{
    UMaterial* Material = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
    return Material ? Material->GetPathName() : FString();
}

static TSharedPtr<FJsonObject> MakeMaterialInterfaceJson(UMaterialInterface* MaterialInterface)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("material_path"), MaterialInterface ? MaterialInterface->GetPathName() : FString());
    Json->SetStringField(TEXT("material_class"), MaterialInterface && MaterialInterface->GetClass() ? MaterialInterface->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("root_material_path"), MaterialRootPath(MaterialInterface));
    Json->SetBoolField(TEXT("is_material_instance"), MaterialInterface && MaterialInterface->IsA<UMaterialInstance>());
    Json->SetBoolField(TEXT("is_dynamic_instance"), MaterialInterface && MaterialInterface->IsA<UMaterialInstanceDynamic>());
    if (UMaterialInstance* Instance = Cast<UMaterialInstance>(MaterialInterface))
    {
        Json->SetStringField(TEXT("parent_path"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
    }
    return Json;
}

static FString MeshAssetPath(UMeshComponent* Component)
{
    if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Component))
    {
        UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh();
        return Mesh ? Mesh->GetPathName() : FString();
    }
    if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Component))
    {
        USkeletalMesh* Mesh = SkeletalMeshComponent->GetSkeletalMeshAsset();
        return Mesh ? Mesh->GetPathName() : FString();
    }
    return FString();
}

static int32 MeshInstanceCount(UMeshComponent* Component)
{
    if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
    {
        return Instanced->GetInstanceCount();
    }
    return 1;
}

TSharedPtr<FJsonObject> MeshComponentToJson(AActor* Actor, UMeshComponent* Component, bool bIncludeMaterials)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("actor_path"), Actor ? Actor->GetPathName() : FString());
    Json->SetStringField(TEXT("actor_label"), Actor ? Actor->GetActorLabel() : FString());
    Json->SetStringField(TEXT("component_path"), Component ? Component->GetPathName() : FString());
    Json->SetStringField(TEXT("component_name"), Component ? Component->GetName() : FString());
    Json->SetStringField(TEXT("component_class"), Component && Component->GetClass() ? Component->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("mesh_asset_path"), MeshAssetPath(Component));
    Json->SetNumberField(TEXT("instance_count"), MeshInstanceCount(Component));
    Json->SetNumberField(TEXT("material_slot_count"), Component ? Component->GetNumMaterials() : 0);

    if (bIncludeMaterials && Component != nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> Materials;
        const TArray<FName> SlotNames = Component->GetMaterialSlotNames();
        for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index)
        {
            TSharedPtr<FJsonObject> Slot = MakeMaterialInterfaceJson(Component->GetMaterial(Index));
            Slot->SetNumberField(TEXT("slot_index"), Index);
            Slot->SetStringField(TEXT("slot_name"), SlotNames.IsValidIndex(Index) ? SlotNames[Index].ToString() : FString());
            Materials.Add(MakeShared<FJsonValueObject>(Slot));
        }
        Json->SetArrayField(TEXT("materials"), Materials);
    }
    return Json;
}

static bool MeshClassMatches(UMeshComponent* Component, const TArray<FString>& ClassNames)
{
    if (ClassNames.Num() == 0)
    {
        return true;
    }
    const FString ClassPath = Component && Component->GetClass() ? Component->GetClass()->GetPathName() : FString();
    for (const FString& ClassName : ClassNames)
    {
        if (ClassPath.Equals(ClassName, ESearchCase::IgnoreCase) || ClassPath.EndsWith(TEXT(".") + ClassName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

static void AppendMeshIndexedLine(FString& Text, int32 Index, AActor* Actor, UMeshComponent* Component)
{
    Text += FString::Printf(
        TEXT("M:%d:%s;a=%s;c=%s;mesh=%s;slots=%d;inst=%d\n"),
        Index,
        Component ? *EscapeIndexedToken(Component->GetPathName()) : TEXT(""),
        Actor ? *EscapeIndexedToken(Actor->GetActorLabel()) : TEXT(""),
        Component && Component->GetClass() ? *EscapeIndexedToken(Component->GetClass()->GetName()) : TEXT(""),
        *EscapeIndexedToken(MeshAssetPath(Component)),
        Component ? Component->GetNumMaterials() : 0,
        MeshInstanceCount(Component));
}

TSharedPtr<FJsonObject> HandleLevelMeshInstancesList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    if (ActorSubsystem == nullptr)
    {
        return MakeInvalidLevelMaterialResponse(Operation, RequestId, TEXT("editor_actor_subsystem_unavailable"), TEXT("UEditorActorSubsystem is not available"));
    }

    TArray<FString> ClassNames;
    Payload->TryGetStringArrayField(TEXT("class_names"), ClassNames);
    bool bIncludeMaterials = false;
    Payload->TryGetBoolField(TEXT("include_materials"), bIncludeMaterials);
    FString Format = TEXT("indexed");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 200, 2000);

    TArray<TSharedPtr<FJsonValue>> Items;
    FString Text;
    int32 Matched = 0;
    int32 Returned = 0;
    bool bHasMore = false;
    for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
    {
        if (Actor == nullptr)
        {
            continue;
        }
        TInlineComponentArray<UMeshComponent*> MeshComponents;
        Actor->GetComponents(MeshComponents);
        for (UMeshComponent* Component : MeshComponents)
        {
            if (Component == nullptr || !MeshClassMatches(Component, ClassNames))
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
            if (bIndexed)
            {
                AppendMeshIndexedLine(Text, Matched - 1, Actor, Component);
            }
            else if (bFull)
            {
                Items.Add(MakeShared<FJsonValueObject>(MeshComponentToJson(Actor, Component, bIncludeMaterials)));
            }
            else
            {
                TArray<TSharedPtr<FJsonValue>> Row;
                Row.Add(MakeShared<FJsonValueString>(Actor->GetPathName()));
                Row.Add(MakeShared<FJsonValueString>(Component->GetPathName()));
                Row.Add(MakeShared<FJsonValueString>(Component->GetClass()->GetName()));
                Row.Add(MakeShared<FJsonValueString>(MeshAssetPath(Component)));
                Row.Add(MakeShared<FJsonValueNumber>(Component->GetNumMaterials()));
                Row.Add(MakeShared<FJsonValueNumber>(MeshInstanceCount(Component)));
                Items.Add(MakeShared<FJsonValueArray>(Row));
            }
            ++Returned;
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (bIndexed)
    {
        Data->SetStringField(TEXT("format"), TEXT("level_mesh_instances_indexed"));
        SetTextPayload(Data, Text);
    }
    else if (!bFull)
    {
        Data->SetStringField(TEXT("format"), TEXT("level_mesh_instances_compact"));
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("actor_path")),
            MakeShared<FJsonValueString>(TEXT("component_path")),
            MakeShared<FJsonValueString>(TEXT("component_class")),
            MakeShared<FJsonValueString>(TEXT("mesh_asset_path")),
            MakeShared<FJsonValueString>(TEXT("material_slot_count")),
            MakeShared<FJsonValueString>(TEXT("instance_count"))
        });
    }
    if (!bIndexed)
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
