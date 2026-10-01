#include "Geometry/NexusGeometry.h"

#include "DynamicMesh/MeshNormals.h"
#include "Math/RandomStream.h"

namespace UeNodeNexusBridge::Geometry
{
bool Noise(FMesh& Mesh, const FObject& Op, FString& Error)
{
    const double Amplitude = Number(Op, TEXT("amplitude"), 100);
    const double Scale = Number(Op, TEXT("scale"), 1000);
    const double Octaves = Number(Op, TEXT("octaves"), 3);
    const double Seed = Number(Op, TEXT("seed"), 0);
    const double Persistence = Number(Op, TEXT("persistence"), 0.5);
    FVector3d Offset = FVector3d::Zero();
    if (!FMath::IsFinite(Amplitude) || FMath::Abs(Amplitude) > 1e6 || !FMath::IsFinite(Scale) || Scale < 0.01 ||
        !FMath::IsFinite(Octaves) || Octaves < 1 || Octaves > 8 || Octaves != FMath::FloorToDouble(Octaves) ||
        !FMath::IsFinite(Seed) || FMath::Abs(Seed) > MAX_int32 || Seed != FMath::FloorToDouble(Seed) ||
        !FMath::IsFinite(Persistence) || Persistence < 0 || Persistence > 1 || !Vector(Op, TEXT("sampling_offset"), Offset, false))
        return Fail(Error, TEXT("invalid noise amplitude/scale/octaves/seed/persistence/sampling_offset"));
    const FString Direction = String(Op, TEXT("direction"), TEXT("z"));
    if (Direction != TEXT("z") && Direction != TEXT("normal")) return Fail(Error, TEXT("noise direction must be z or normal"));
    FRandomStream Random(static_cast<int32>(Seed));
    const FVector3d Phase(Random.FRand() * 1000, Random.FRand() * 1000, Random.FRand() * 1000);
    UE::Geometry::FMeshNormals Normals(&Mesh);
    Normals.ComputeVertexNormals();
    for (int32 ID : Mesh.VertexIndicesItr())
    {
        const double Weight = Mask(Mesh, ID, Op);
        if (!FMath::IsFinite(Weight)) return Fail(Error, TEXT("invalid noise mask"));
        const FVector3d Position = Mesh.GetVertex(ID);
        FVector3d Sample = (Position + Offset) / Scale + Phase;
        double Value = 0, Sum = 0, Gain = 1;
        for (int32 Octave = 0; Octave < static_cast<int32>(Octaves); ++Octave)
        {
            Value += FMath::PerlinNoise3D(FVector(Sample)) * Gain;
            Sum += Gain;
            Sample *= 2;
            Gain *= Persistence;
        }
        const FVector3d Axis = Direction == TEXT("normal") ? Normals[ID] : FVector3d(0, 0, 1);
        Mesh.SetVertex(ID, Position + Axis * (Amplitude * Value / Sum * Weight));
    }
    return true;
}
}
