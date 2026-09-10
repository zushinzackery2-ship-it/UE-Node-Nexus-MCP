#pragma once

#include "NexusSceneWorld.h"

namespace UeNodeNexusBridge::Scene
{
struct FSaveTarget
{
    UPackage* Package = nullptr;
    UObject* Base = nullptr;
    bool bDeleted = false;
};

struct FApply
{
    UWorld* World = nullptr;
    FObject Plan;
    FObject Before;
    TUniquePtr<FActorIndex> WorldIndex;
    TMap<FString, AActor*> Actors;
    TMap<FString, UClass*> Classes;
    TSet<FString> CreatedActors;
    TMap<FString, UClass*> ComponentTypes;
    TMap<FString, UActorComponent*> ComponentTemplates;
    TMap<FString, FString> ComponentNames;
    TMap<FString, FObject> ComponentSnapshots;
    TSet<FString> CreatedComponents;
    TSet<FString> RemovedComponents;
    TMap<FString, FSaveTarget> Packages;
    FRows Results;
    FString Error;
    bool bChanged = false;

    void Touch(AActor* Actor, bool bDeleted = false);
};

bool Preflight(FApply& Context);
bool CheckComponent(FApply& Context, const FObject& Op);
bool CheckHierarchy(FApply& Context);
bool PrepareResources(FApply& Context, bool bReadPlan = true);
bool ApplyActor(FApply& Context, const FObject& Op);
bool ApplyComponent(FApply& Context, const FObject& Op);
bool SaveScene(FApply& Context, FRows& Saved, FRows& Failed);
FObject ApplyScene(UWorld* World, const FObject& Plan, bool bDryRun, bool bSave, FString& Error);
}
