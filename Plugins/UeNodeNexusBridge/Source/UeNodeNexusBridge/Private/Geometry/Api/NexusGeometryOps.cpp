#include "Geometry/NexusGeometry.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Misc/PackageName.h"
#include "Operations/FFDLattice.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Geometry;

TSharedPtr<FJsonObject> HandleMeshGeometryGet(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *String(Payload, TEXT("asset_path")));
    if (!Asset || !Asset->GetMeshDescription(0)) return MakeOperationError(Operation, RequestId, TEXT("mesh_unavailable"), TEXT("LOD0 MeshDescription required"));
    FMesh Mesh;
    FMeshDescriptionToDynamicMesh Converter;
    Converter.Convert(Asset->GetMeshDescription(0), Mesh);
    FObject Data = Inspect(Asset);
    Data->SetObjectField(TEXT("quality"), Quality(Mesh));
    if (Payload->HasField(TEXT("lattice_dimensions")))
    {
        FVector3d Dims;
        if (!Vector(Payload, TEXT("lattice_dimensions"), Dims)) return MakeOperationError(Operation, RequestId, TEXT("invalid_lattice"), TEXT("dimensions require 3 integers"));
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Dims[Axis] < 2 || Dims[Axis] > 32 || Dims[Axis] != FMath::FloorToDouble(Dims[Axis]))
                return MakeOperationError(Operation, RequestId, TEXT("invalid_lattice"), TEXT("dimensions must be 2..32"));
        }
        UE::Geometry::FFFDLattice Cage(UE::Geometry::FVector3i(static_cast<int32>(Dims.X), static_cast<int32>(Dims.Y), static_cast<int32>(Dims.Z)), Mesh, 0.01f);
        TArray<FVector3d> Positions;
        Cage.GenerateInitialLatticePositions(Positions);
        TArray<TSharedPtr<FJsonValue>> Points;
        for (const auto& P : Positions)
        {
            TArray<TSharedPtr<FJsonValue>> Coordinates;
            for (int32 Axis = 0; Axis < 3; ++Axis) Coordinates.Add(MakeShared<FJsonValueNumber>(P[Axis]));
            Points.Add(MakeShared<FJsonValueArray>(Coordinates));
        }
        Data->SetArrayField(TEXT("lattice_points"), Points);
        Data->SetStringField(TEXT("lattice_index_order"), TEXT("k + nz * (j + ny * i); positions in asset-local cm"));
    }
    FObject Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleMeshGeometryBuild(const FString& Operation, const FString& RequestId, const FObject& Payload)
{
    if (!GEditor || GEditor->PlayWorld) return MakeOperationError(Operation, RequestId, TEXT("editor_unavailable"), TEXT("requires editor outside PIE"));
    const double Started = FPlatformTime::Seconds();
    FContext Context;
    Context.Deadline = Started + 20;
    FString Error;
    const FString Path = String(Payload, TEXT("output_asset"));
    if (!Path.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Path) || Path.Contains(TEXT(".")))
        return MakeOperationError(Operation, RequestId, TEXT("invalid_output"), TEXT("output_asset must be a /Game package path"));
    if (!ValidateRecipe(Child(Payload, TEXT("recipe")), Error) ||
        !LoadSource(Child(Payload, TEXT("recipe")), Context, Error) ||
        !CheckDestination(Context, Path, Error) || !Process(Context, Error))
    {
        UE_LOG(LogTemp, Display, TEXT("[NexusGeometry] %s rejected: %s"), *RequestId, *Error);
        return MakeOperationError(Operation, RequestId, TEXT("geometry_rejected"), Error);
    }
    const bool DryRun = Boolean(Payload, TEXT("dry_run"), true);
    FObject Data = MakeShared<FJsonObject>();
    bool Success = true;
    if (!DryRun)
    {
        FString Text;
        FJsonSerializer::Serialize(Context.Recipe.ToSharedRef(), TJsonWriterFactory<>::Create(&Text));
        Success = Publish(Context, Path, Text, Boolean(Payload, TEXT("save"), false), Data, Error);
    }
    Data->SetBoolField(TEXT("dry_run"), DryRun);
    Data->SetStringField(TEXT("output_asset"), Path);
    Data->SetObjectField(TEXT("quality"), Quality(Context.Mesh));
    Data->SetArrayField(TEXT("steps"), Context.Steps);
    Data->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Started);
    FObject Response = MakeEnvelope(Operation, RequestId, Success);
    Response->SetObjectField(TEXT("data"), Data);
    if (!Success) Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(FString(TEXT("geometry_publish_failed")), Error));
    UE_LOG(LogTemp, Display, TEXT("[NexusGeometry] %s output=%s triangles=%d success=%d seconds=%.3f"),
        *RequestId, *Path, Context.Mesh.TriangleCount(), Success, FPlatformTime::Seconds() - Started);
    return Response;
}
}
