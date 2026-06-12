#include "UeNodeNexusBridgeLandscapeGrassTypeOps.h"

#include "FileHelpers.h"
#include "LandscapeGrassType.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
namespace
{
void ApplyGrassTypePatches(ULandscapeGrassType* GrassType, const TArray<FGrassVarietyPatch>& Patches)
{
    GrassType->Modify();
    for (const FGrassVarietyPatch& Patch : Patches)
    {
        FGrassVariety& Variety = GrassType->GrassVarieties[Patch.Index];
        Variety.Scaling = Patch.NewScaling;
        Variety.ScaleX = Patch.NewScaleX;
        Variety.ScaleY = Patch.NewScaleY;
        Variety.ScaleZ = Patch.NewScaleZ;
    }

    GrassType->MarkPackageDirty();
#if WITH_EDITOR
    FPropertyChangedEvent ChangeEvent(nullptr, EPropertyChangeType::ValueSet);
    GrassType->PostEditChangeProperty(ChangeEvent);
#endif
}
}

TSharedPtr<FJsonObject> HandleLandscapeGrassTypeSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> LoadError;
    ULandscapeGrassType* GrassType = LoadAssetOrError<ULandscapeGrassType>(Payload, Operation, RequestId, LoadError, TEXT("LandscapeGrassType"));
    if (GrassType == nullptr)
    {
        return LoadError;
    }

    const TArray<TSharedPtr<FJsonValue>>* VarietyValues = nullptr;
    if (!Payload->TryGetArrayField(TEXT("varieties"), VarietyValues) || VarietyValues == nullptr || VarietyValues->Num() == 0)
    {
        return MakeGrassTypeError(Operation, RequestId, TEXT("invalid_request"), TEXT("varieties must be a non-empty array"));
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    TArray<FGrassVarietyPatch> Patches;
    Patches.Reserve(VarietyValues->Num());
    for (const TSharedPtr<FJsonValue>& Value : *VarietyValues)
    {
        TSharedPtr<FJsonObject> VarietyPayload = Value.IsValid() ? Value->AsObject() : nullptr;
        if (!VarietyPayload.IsValid())
        {
            return MakeGrassTypeError(Operation, RequestId, TEXT("invalid_variety_item"), TEXT("each varieties item must be an object"));
        }

        FGrassVarietyPatch Patch;
        TSharedPtr<FJsonObject> PatchError;
        if (!BuildGrassVarietyPatch(GrassType, VarietyPayload, Operation, RequestId, Patch, PatchError))
        {
            return PatchError;
        }
        Patches.Add(Patch);
    }

    const bool bChanged = AnyGrassTypePatchChanged(Patches);
    TArray<UPackage*> PackagesToSave;
    bool bApplied = false;
    bool bSaved = !bSave;
    if (!bDryRun)
    {
        if (bChanged)
        {
            ApplyGrassTypePatches(GrassType, Patches);
            PackagesToSave.AddUnique(GrassType->GetOutermost());
        }
        bApplied = true;
        bSaved = !bSave || PackagesToSave.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
    }

    TSharedPtr<FJsonObject> DirtyState = MakeDirtyState(GrassType);
    DirtyState->SetBoolField(TEXT("saved"), bSave && bSaved);
    TSharedPtr<FJsonObject> Data = MakeWriteData(
        bDryRun,
        bApplied,
        bChanged,
        MakeGrassTypeDiff(Patches, bApplied),
        MakePinIntegrity(true, {}, {}),
        MakeCompilePostCheck(false, false, true, 0, 0),
        DirtyState);
    Data->SetStringField(TEXT("asset_path"), GrassType->GetPathName());
    Data->SetNumberField(TEXT("variety_count"), GrassType->GrassVarieties.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bDryRun || !bSave || bSaved);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Response->GetBoolField(TEXT("ok")))
    {
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("save_failed"), TEXT("GrassType scale changes were applied but the package could not be saved")));
    }
    return Response;
}
}
