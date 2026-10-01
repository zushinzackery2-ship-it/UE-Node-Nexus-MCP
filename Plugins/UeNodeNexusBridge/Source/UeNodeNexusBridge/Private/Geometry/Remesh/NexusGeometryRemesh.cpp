#include "Geometry/NexusGeometry.h"

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"
#include "Remesher.h"

namespace UeNodeNexusBridge::Geometry
{
static bool SmoothPass(FMesh& Mesh, UE::Geometry::FRemesher& Worker, UE::Geometry::FMeshProjectionTarget& Projection, FString& Error)
{
    // UE's buffered smoothing does not apply bPreventNormalFlips. Use sequential
    // projected relaxation with an orientation barrier around each vertex.
    for (int32 ID : Mesh.VertexIndicesItr())
    {
        const auto& Constraints = Worker.GetConstraints();
        if (Constraints.IsSet() && !Constraints->GetVertexConstraint(ID).bCanMove) continue;
        const FVector3d Position = Mesh.GetVertex(ID);
        FVector3d Average = FVector3d::Zero();
        int32 Count = 0;
        for (int32 Neighbor : Mesh.VtxVerticesItr(ID))
        {
            Average += Mesh.GetVertex(Neighbor);
            ++Count;
        }
        if (!Count) continue;
        const FVector3d Target = Projection.Project(FMath::Lerp(Position, Average / Count, 0.25));
        double Fraction = 1;
        for (int32 Triangle : Mesh.VtxTrianglesItr(ID))
        {
            const auto Vertices = Mesh.GetTriangle(Triangle);
            const FVector3d A = Mesh.GetVertex(Vertices.A), B = Mesh.GetVertex(Vertices.B), C = Mesh.GetVertex(Vertices.C);
            const FVector3d Normal = (B - A).Cross(C - A);
            const FVector3d TA = Vertices.A == ID ? Target : A, TB = Vertices.B == ID ? Target : B, TC = Vertices.C == ID ? Target : C;
            const double Before = Normal.SquaredLength(), After = (TB - TA).Cross(TC - TA).Dot(Normal);
            if (After < Before * 0.1) Fraction = FMath::Min(Fraction, 0.8 * Before / (Before - After));
        }
        // Retain the feasible displacement. Re-projecting the clipped point
        // would undo the orientation barrier on a curved reference surface.
        const FVector3d Candidate = FMath::Lerp(Position, Target, Fraction);
        for (int32 Triangle : Mesh.VtxTrianglesItr(ID))
        {
            const auto Vertices = Mesh.GetTriangle(Triangle);
            const FVector3d A = Mesh.GetVertex(Vertices.A), B = Mesh.GetVertex(Vertices.B), C = Mesh.GetVertex(Vertices.C);
            const FVector3d TA = Vertices.A == ID ? Candidate : A, TB = Vertices.B == ID ? Candidate : B, TC = Vertices.C == ID ? Candidate : C;
            if ((TB - TA).Cross(TC - TA).Dot((B - A).Cross(C - A)) <= 1e-12)
                return Fail(Error, TEXT("projected relaxation would invert a triangle; adjust remesh target"));
        }
        Mesh.SetVertex(ID, Candidate);
    }
    return true;
}

bool Remesh(FContext& Context, const FObject& Op, FString& Error)
{
    using namespace UE::Geometry;
    const double Length = Number(Op, TEXT("target_edge_length"), 100);
    const double Iterations = Number(Op, TEXT("iterations"), 5);
    if (!FMath::IsFinite(Length) || Length <= 0 || !FMath::IsFinite(Iterations) ||
        Iterations < 1 || Iterations > 20 || Iterations != FMath::FloorToDouble(Iterations))
        return Fail(Error, TEXT("remesh requires positive target_edge_length and integer iterations 1..20"));
    const FObject Stats = Quality(Context.Mesh);
    if (Boolean(Op, TEXT("automatic"), false) && Stats->GetNumberField(TEXT("max_edge")) <= Length * 1.5 &&
        Stats->GetNumberField(TEXT("max_aspect")) <= 3.0) return true;
    if (Stats->GetNumberField(TEXT("surface_area")) / (Length * Length * 0.433) > Context.MaxTriangles)
        return Fail(Error, TEXT("target edge length exceeds triangle budget; increase length or budget"));
    FMesh Reference(Context.Mesh);
    FDynamicMeshAABBTree3 Tree(&Reference, true);
    FMeshProjectionTarget Projection(&Reference, &Tree);
    FRemesher Worker(&Context.Mesh);
    Worker.SetTargetEdgeLength(Length);
    Worker.bPreventNormalFlips = true;
    Worker.bPreventTinyTriangles = true;
    Worker.bEnableParallelSmooth = false;
    Worker.bEnableParallelProjection = false;
    Worker.bEnableSmoothInPlace = false;
    Worker.bEnableSmoothing = false;
    Worker.SmoothSpeedT = 0.25;
    Worker.SetProjectionTarget(&Projection);
    // The engine's whole-mesh projection pass has no orientation barrier.
    // Project only through the sequential, checked relaxation below.
    Worker.ProjectionMode = FMeshRefinerBase::ETargetProjectionMode::NoProjection;
    FMeshConstraints Constraints;
    FMeshConstraintsUtil::ConstrainAllBoundariesAndSeams(Constraints, Context.Mesh,
        Boolean(Op, TEXT("lock_boundary"), true) ? EEdgeRefineFlags::FullyConstrained : EEdgeRefineFlags::NoFlip,
        EEdgeRefineFlags::NoFlip, EEdgeRefineFlags::NoFlip, true, false, false, false);
    const FObject Region = Child(Op, TEXT("region"));
    if (Region)
    {
        FVector3d Min, Max;
        if (!Vector(Region, TEXT("min"), Min) || !Vector(Region, TEXT("max"), Max) ||
            Min.X >= Max.X || Min.Y >= Max.Y || Min.Z >= Max.Z) return Fail(Error, TEXT("invalid remesh region"));
        for (int32 Edge : Context.Mesh.EdgeIndicesItr())
        {
            const FIndex2i Vertices = Context.Mesh.GetEdgeV(Edge);
            bool Outside = false;
            for (int32 ID : {Vertices.A, Vertices.B})
            {
                const FVector3d P = Context.Mesh.GetVertex(ID);
                Outside |= P.X < Min.X || P.X > Max.X || P.Y < Min.Y || P.Y > Max.Y || P.Z < Min.Z || P.Z > Max.Z;
            }
            if (Outside)
            {
                Constraints.SetOrUpdateEdgeConstraint(Edge, FEdgeConstraint::FullyConstrained());
                Constraints.SetOrUpdateVertexConstraint(Vertices.A, FVertexConstraint::FullyConstrained());
                Constraints.SetOrUpdateVertexConstraint(Vertices.B, FVertexConstraint::FullyConstrained());
            }
        }
    }
    Worker.SetExternalConstraints(MoveTemp(Constraints));
    Worker.Precompute();
    for (int32 Pass = 0; Pass < static_cast<int32>(Iterations); ++Pass)
    {
        if (FPlatformTime::Seconds() > Context.Deadline) return Fail(Error, TEXT("geometry compute time budget exceeded"));
        // A split pass can create up to roughly four times as many triangles.
        if (Context.Mesh.TriangleCount() * 4 > Context.MaxTriangles &&
            Quality(Context.Mesh)->GetNumberField(TEXT("max_edge")) > Length * 4.0 / 3.0)
            return Fail(Error, TEXT("insufficient triangle budget for a bounded remesh pass"));
        Worker.BasicRemeshPass();
        if (!SmoothPass(Context.Mesh, Worker, Projection, Error)) return false;
        if (Context.Mesh.TriangleCount() > Context.MaxTriangles) return Fail(Error, TEXT("remesh triangle budget exceeded"));
    }
    Context.Mesh.CompactInPlace();
    return true;
}
}
