#include "UeNodeNexusBridgeBlueprintComponentDefaults.h"

#include "Components/MeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialDefault(
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
}
