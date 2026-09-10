#include "NexusSceneApply.h"

#include "NexusSceneProperties.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshCompiler.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Scene
{
static void GatherComponent(UActorComponent* Component, TSet<UObject*>& References)
{
    if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component))
    {
        if (Mesh->GetStaticMesh())
        {
            References.Add(Mesh->GetStaticMesh());
        }
    }
    if (UMeshComponent* Mesh = Cast<UMeshComponent>(Component))
    {
        for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
        {
            if (UMaterialInterface* Material = Mesh->GetMaterial(Slot))
            {
                References.Add(Material);
            }
        }
    }
}

static void GatherActor(AActor* Actor, TSet<UObject*>& References)
{
    if (IsValid(Actor))
    {
        for (UActorComponent* Component : Actor->GetComponents())
        {
            GatherComponent(Component, References);
        }
    }
}

bool PrepareResources(FApply& Context, bool bReadPlan)
{
    const double Started = FPlatformTime::Seconds();
    TSet<UObject*> References;
    TSet<UClass*> Classes;
    for (const auto& Pair : Context.Classes)
    {
        Classes.Add(Pair.Value);
    }
    for (UClass* Class : Classes)
    {
        GatherActor(Cast<AActor>(Class->GetDefaultObject()), References);
        for (UClass* Type = Class; Type; Type = Type->GetSuperClass())
        {
            UBlueprint* Blueprint = Cast<UBlueprint>(Type->ClassGeneratedBy);
            if (Blueprint && Blueprint->SimpleConstructionScript)
            {
                for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
                {
                    GatherComponent(Node->ComponentTemplate, References);
                }
            }
        }
    }
    for (const auto& Pair : Context.Actors)
    {
        GatherActor(Pair.Value, References);
    }
    for (const auto& Value : Rows(Context.Plan, TEXT("ops")))
    {
        if (!bReadPlan)
        {
            break;
        }
        const FObject Op = Value->AsObject();
        const FObject Properties = Object(Op, TEXT("properties"));
        if (Properties->Values.IsEmpty())
        {
            continue;
        }
        const FString Id = String(Op, TEXT("id"));
        UObject* Target = nullptr;
        if (String(Op, TEXT("op")).EndsWith(TEXT("_actor")))
        {
            Target = Context.Actors.FindRef(Id);
            Target = Target ? Target : Context.Classes[Id]->GetDefaultObject();
        }
        else
        {
            Target = Context.ComponentTemplates.FindRef(String(Op, TEXT("actor_id")) + TEXT("/") + Id);
        }
        if (!Target || !GatherPropertyReferences(Target, Properties, References, Context.Error))
        {
            return false;
        }
    }
    TArray<UStaticMesh*> Meshes;
    TSet<UMaterialInterface*> Materials;
    for (UObject* Object : References)
    {
        if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
        {
            Meshes.Add(Mesh);
        }
        if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object))
        {
            Materials.Add(Material);
        }
    }
    FStaticMeshCompilingManager::Get().FinishCompilation(MakeArrayView(Meshes));
    for (UStaticMesh* Mesh : Meshes)
    {
        for (const FStaticMaterial& Slot : Mesh->GetStaticMaterials())
        {
            if (Slot.MaterialInterface)
            {
                Materials.Add(Slot.MaterialInterface);
            }
        }
    }
    for (UMaterialInterface* Material : Materials)
    {
        if (!MaterialResourceStatus(Material, true, false).bOk)
        {
            Context.Error = TEXT("scene_material_not_ready: ") + Material->GetPathName();
            return false;
        }
    }
    FinishRenderingUpdates();
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s phase=scene_resources meshes=%d materials=%d duration_ms=%.3f"),
        *ActiveBridgeRequestId(), Meshes.Num(), Materials.Num(), (FPlatformTime::Seconds() - Started) * 1000.0);
    return true;
}
}
