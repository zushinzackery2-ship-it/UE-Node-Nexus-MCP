#include "Geometry/NexusGeometry.h"

#include "Operations/FFDLattice.h"

namespace UeNodeNexusBridge::Geometry
{
double Mask(const FMesh& Mesh, int32 Vertex, const FObject& Op)
{
    if (Boolean(Op, TEXT("lock_boundary"), false) && Mesh.IsBoundaryVertex(Vertex)) return 0;
    const FVector3d P = Mesh.GetVertex(Vertex);
    const FObject Spec = Child(Op, TEXT("mask"));
    if (!Spec) return 1;
    FVector3d Center, Radius;
    if (!Vector(Spec, TEXT("center"), Center) || !Vector(Spec, TEXT("radius"), Radius) || Radius.GetMin() <= 0) return NAN;
    const double D = ((P - Center) / Radius).Length();
    const double T = FMath::Clamp(1.0 - D, 0.0, 1.0);
    const double Value = T * T * (3.0 - 2.0 * T);
    return Boolean(Spec, TEXT("invert"), false) ? 1.0 - Value : Value;
}

bool Lattice(FMesh& Mesh, const FObject& Op, FString& Error)
{
    using namespace UE::Geometry;
    FVector3d Dims;
    if (!Vector(Op, TEXT("dimensions"), Dims)) return Fail(Error, TEXT("lattice dimensions require [x,y,z]"));
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (Dims[Axis] < 2 || Dims[Axis] > 32 || Dims[Axis] != FMath::FloorToDouble(Dims[Axis]))
            return Fail(Error, TEXT("lattice dimensions must be integers 2..32"));
    }
    const FVector3i Counts(static_cast<int32>(Dims.X), static_cast<int32>(Dims.Y), static_cast<int32>(Dims.Z));
    FFFDLattice Cage(Counts, Mesh, 0.01f);
    TArray<FVector3d> Controls;
    Cage.GenerateInitialLatticePositions(Controls);
    const TArray<TSharedPtr<FJsonValue>>* Offsets = nullptr;
    if (!Op->TryGetArrayField(TEXT("offsets"), Offsets) || Offsets->Num() > Controls.Num())
        return Fail(Error, TEXT("lattice offsets require unique control point index and delta vectors"));
    TSet<int32> Changed;
    for (const auto& Value : *Offsets)
    {
        const FObject* Item = nullptr;
        FVector3d Index, Delta;
        if (!Value->TryGetObject(Item) || !Vector(*Item, TEXT("index"), Index) || !Vector(*Item, TEXT("delta"), Delta))
            return Fail(Error, TEXT("invalid lattice offset"));
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Index[Axis] < 0 || Index[Axis] >= Dims[Axis] || Index[Axis] != FMath::FloorToDouble(Index[Axis]))
                return Fail(Error, TEXT("lattice control index out of bounds"));
        }
        const int32 ID = Cage.ControlPointIndexFromCoordinates(static_cast<int32>(Index.X), static_cast<int32>(Index.Y), static_cast<int32>(Index.Z));
        if (Changed.Contains(ID)) return Fail(Error, TEXT("duplicate lattice control index"));
        Changed.Add(ID);
        Controls[ID] += Delta;
    }
    const FString Interpolation = String(Op, TEXT("interpolation"), TEXT("cubic"));
    if (Interpolation != TEXT("linear") && Interpolation != TEXT("cubic")) return Fail(Error, TEXT("interpolation must be linear or cubic"));
    TArray<FVector3d> Positions;
    Cage.GetDeformedMeshVertexPositions(Controls, Positions, Interpolation == TEXT("cubic") ? ELatticeInterpolation::Cubic : ELatticeInterpolation::Linear);
    for (int32 ID : Mesh.VertexIndicesItr())
    {
        const double Weight = Mask(Mesh, ID, Op);
        if (!FMath::IsFinite(Weight)) return Fail(Error, TEXT("mask requires finite center and positive radius vectors"));
        Mesh.SetVertex(ID, FMath::Lerp(Mesh.GetVertex(ID), Positions[ID], Weight));
    }
    return true;
}
}
