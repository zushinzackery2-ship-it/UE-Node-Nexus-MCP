#pragma once

#include "Level/Scene/NexusSceneJson.h"

class UInstancedStaticMeshComponent;

namespace UeNodeNexusBridge::Instances
{
struct FInstance
{
    FGuid Id;
    FTransform Transform = FTransform::Identity;
    TArray<float> CustomData;
};

bool Export(UInstancedStaticMeshComponent* Component, Scene::FObject& Out, FString& Error,
    bool bRebind = false, int32 Offset = 0, int32 Limit = MAX_int32);
bool Parse(const Scene::FRows& Rows, int32 CustomCount, TArray<FInstance>& Out, FString& Error);
bool Apply(UInstancedStaticMeshComponent* Component, const TArray<FInstance>& Desired, int32 CustomCount, FString& Error);
}
