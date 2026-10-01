#include "Geometry/NexusGeometry.h"

#include "DynamicMesh/DynamicMeshAABBTree3.h"

namespace UeNodeNexusBridge::Geometry
{
// Resolve candidate pairs to intersection geometry so coplanar overlaps are
// rejected while zero-area contacts between disconnected pieces remain valid.
static bool Intersects(UE::Geometry::FIntrTriangle3Triangle3d& Query)
{
    Query.SetReportCoplanarIntersection(true);
    if (!Query.Find() || Query.Quantity < 2) return false;
    const auto A = Query.GetTriangle0(), B = Query.GetTriangle1();
    const FVector3d NA = (A.V[1] - A.V[0]).Cross(A.V[2] - A.V[0]).GetSafeNormal();
    const FVector3d NB = (B.V[1] - B.V[0]).Cross(B.V[2] - B.V[0]).GetSafeNormal();
    if (NA.Cross(NB).SquaredLength() < 1e-12)
    {
        double Area = 0;
        for (int32 Index = 1; Index + 1 < Query.Quantity; ++Index)
        {
            Area += (Query.Points[Index] - Query.Points[0]).Cross(Query.Points[Index + 1] - Query.Points[0]).Length();
        }
        return Area > 1e-8;
    }
    const bool Hit = (Query.Points[1] - Query.Points[0]).SquaredLength() > 1e-12;
    if (Hit)
    {
        UE_LOG(LogTemp, Display, TEXT("[NexusGeometry] intersection A=(%s;%s;%s) B=(%s;%s;%s) segment=(%s;%s)"),
            *A.V[0].ToString(), *A.V[1].ToString(), *A.V[2].ToString(),
            *B.V[0].ToString(), *B.V[1].ToString(), *B.V[2].ToString(),
            *Query.Points[0].ToString(), *Query.Points[1].ToString());
    }
    return Hit;
}

bool CheckDeformation(const FMesh& Before, const FMesh& After, FString& Error)
{
    for (int32 ID : Before.TriangleIndicesItr())
    {
        if (Before.GetTriNormal(ID).Dot(After.GetTriNormal(ID)) <= 0)
            return Fail(Error, TEXT("triangle orientation reversed; reduce deformation or use smaller steps"));
    }
    return true;
}

FObject Quality(const FMesh& Mesh)
{
    double MaxEdge = 0, MinEdge = TNumericLimits<double>::Max(), MaxAspect = 0, Area = 0;
    int32 Degenerate = 0;
    for (int32 ID : Mesh.TriangleIndicesItr())
    {
        const UE::Geometry::FIndex3i T = Mesh.GetTriangle(ID);
        const FVector3d A = Mesh.GetVertex(T.A), B = Mesh.GetVertex(T.B), C = Mesh.GetVertex(T.C);
        const double AB = (A - B).Length(), BC = (B - C).Length(), CA = (C - A).Length();
        const double Longest = FMath::Max3(AB, BC, CA);
        const double TriangleArea = (B - A).Cross(C - A).Length() * 0.5;
        MaxEdge = FMath::Max(MaxEdge, Longest);
        MinEdge = FMath::Min(MinEdge, FMath::Min3(AB, BC, CA));
        Area += TriangleArea;
        if (TriangleArea < 1e-8) ++Degenerate;
        MaxAspect = FMath::Max(MaxAspect, Longest * Longest / FMath::Max(TriangleArea * 2, 1e-8));
    }
    const auto Bounds = Mesh.GetBounds();
    FObject Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("vertices"), Mesh.VertexCount());
    Result->SetNumberField(TEXT("triangles"), Mesh.TriangleCount());
    Result->SetNumberField(TEXT("max_edge"), MaxEdge);
    Result->SetNumberField(TEXT("min_edge"), Mesh.TriangleCount() ? MinEdge : 0);
    Result->SetNumberField(TEXT("max_aspect"), MaxAspect);
    Result->SetNumberField(TEXT("surface_area"), Area);
    Result->SetNumberField(TEXT("degenerate_triangles"), Degenerate);
    for (const auto& Pair : {TPair<FString, FVector3d>(TEXT("min"), Bounds.Min), TPair<FString, FVector3d>(TEXT("max"), Bounds.Max)})
    {
        TArray<TSharedPtr<FJsonValue>> Coordinates;
        for (int32 Axis = 0; Axis < 3; ++Axis) Coordinates.Add(MakeShared<FJsonValueNumber>(Pair.Value[Axis]));
        Result->SetArrayField(Pair.Key, Coordinates);
    }
    return Result;
}

bool Validate(const FMesh& Mesh, int32 MaxTriangles, FString& Error)
{
    if (Mesh.TriangleCount() < 1 || Mesh.TriangleCount() > MaxTriangles) return Fail(Error, TEXT("mesh triangle budget exceeded or empty mesh"));
    for (int32 ID : Mesh.VertexIndicesItr())
    {
        const FVector3d P = Mesh.GetVertex(ID);
        if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z) || P.GetAbsMax() > 1e8)
            return Fail(Error, TEXT("non-finite or out-of-range vertex"));
    }
    if (Quality(Mesh)->GetNumberField(TEXT("degenerate_triangles")) > 0) return Fail(Error, TEXT("degenerate triangles detected"));
    UE::Geometry::FDynamicMeshAABBTree3 Tree(&Mesh, true);
    if (Tree.find_self_intersections(nullptr, true, Intersects, UE::Geometry::IMeshSpatial::FQueryOptions()))
        return Fail(Error, TEXT("self-intersecting mesh; reduce deformation"));
    return true;
}
}
