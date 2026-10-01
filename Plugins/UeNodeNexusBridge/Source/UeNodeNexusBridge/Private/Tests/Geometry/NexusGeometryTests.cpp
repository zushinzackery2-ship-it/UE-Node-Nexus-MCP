#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Geometry/NexusGeometry.h"
#include "Serialization/JsonSerializer.h"

using namespace UeNodeNexusBridge::Geometry;

namespace
{
FObject Json(const TCHAR* Text)
{
    FObject Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    return Result;
}

bool Plane(FContext& Context, FString& Error)
{
    Context.Deadline = FPlatformTime::Seconds() + 20;
    return LoadSource(Json(TEXT(R"({"grid":{"size":[1000,1000,0],"cells":[12,12,0]},"ops":[]})")), Context, Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusLatticeTest, "Nexus.Geometry.LatticeBoundaryAndIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusLatticeTest::RunTest(const FString& Parameters)
{
    FContext Context;
    FString Error;
    if (!TestTrue(TEXT("plane"), Plane(Context, Error))) return false;
    for (int32 ID : Context.Mesh.TriangleIndicesItr())
    {
        if (!TestTrue(TEXT("UE left-handed grid faces upward"), Context.Mesh.GetTriNormal(ID).Z > 0.99)) return false;
    }
    const FMesh Before(Context.Mesh);
    TestTrue(TEXT("identity lattice"), Lattice(Context.Mesh, Json(TEXT(R"({"dimensions":[3,3,2],"offsets":[]})")), Error));
    for (int32 ID : Before.VertexIndicesItr()) TestTrue(TEXT("identity position"), (Before.GetVertex(ID) - Context.Mesh.GetVertex(ID)).Length() < 0.01);
    TestTrue(TEXT("raise centre"), Lattice(Context.Mesh, Json(TEXT(R"({"dimensions":[3,3,2],"offsets":[{"index":[1,1,0],"delta":[0,0,200]},{"index":[1,1,1],"delta":[0,0,200]}],"lock_boundary":true})")), Error));
    const double CentreHeight = Quality(Context.Mesh)->GetArrayField(TEXT("max"))[2]->AsNumber();
    TestTrue(TEXT("cubic weighted centre raised"), CentreHeight > 50 && CentreHeight < 200);
    for (int32 ID : Before.VertexIndicesItr())
    {
        if (Before.IsBoundaryVertex(ID)) TestTrue(TEXT("boundary fixed"), (Before.GetVertex(ID) - Context.Mesh.GetVertex(ID)).Length() < 0.01);
    }
    TestTrue(TEXT("valid deformation"), Validate(Context.Mesh, Context.MaxTriangles, Error));
    TestFalse(TEXT("bad control rejected"), Lattice(Context.Mesh, Json(TEXT(R"({"dimensions":[3,3,2],"offsets":[{"index":[3,0,0],"delta":[0,0,1]}]})")), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNoiseTest, "Nexus.Geometry.NoiseDeterminismAndMask", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusNoiseTest::RunTest(const FString& Parameters)
{
    FContext A, B;
    FString Error;
    if (!Plane(A, Error) || !Plane(B, Error)) return false;
    const FObject Op = Json(TEXT(R"({"amplitude":80,"scale":300,"seed":12,"lock_boundary":true,"mask":{"center":[0,0,0],"radius":[400,400,400]}})"));
    TestTrue(TEXT("noise A"), Noise(A.Mesh, Op, Error));
    TestTrue(TEXT("noise B"), Noise(B.Mesh, Op, Error));
    double Maximum = 0;
    for (int32 ID : A.Mesh.VertexIndicesItr())
    {
        TestTrue(TEXT("deterministic"), A.Mesh.GetVertex(ID) == B.Mesh.GetVertex(ID));
        Maximum = FMath::Max(Maximum, FMath::Abs(A.Mesh.GetVertex(ID).Z));
        if (A.Mesh.IsBoundaryVertex(ID)) TestEqual(TEXT("fixed boundary"), A.Mesh.GetVertex(ID).Z, 0.0);
    }
    TestTrue(TEXT("nonzero displacement"), Maximum > 0.01);
    TestFalse(TEXT("zero scale rejected"), Noise(A.Mesh, Json(TEXT(R"({"scale":0})")), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRemeshTest, "Nexus.Geometry.RemeshShapeAndBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusRemeshTest::RunTest(const FString& Parameters)
{
    FContext Context;
    FString Error;
    if (!Plane(Context, Error)) return false;
    const int32 Before = Context.Mesh.TriangleCount();
    TestTrue(TEXT("remesh"), Remesh(Context, Json(TEXT(R"({"target_edge_length":50,"iterations":4})")), Error));
    TestTrue(TEXT("adds samples"), Context.Mesh.TriangleCount() > Before);
    const bool Valid = Validate(Context.Mesh, Context.MaxTriangles, Error);
    TestTrue(TEXT("valid remesh: ") + Error, Valid);
    for (int32 ID : Context.Mesh.VertexIndicesItr()) TestTrue(TEXT("projection holds surface"), FMath::Abs(Context.Mesh.GetVertex(ID).Z) < 0.01);
    TestFalse(TEXT("budget rejection"), Remesh(Context, Json(TEXT(R"({"target_edge_length":0.01})")), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGeometryInvalidTest, "Nexus.Geometry.InputAndSelfIntersection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusGeometryInvalidTest::RunTest(const FString& Parameters)
{
    FContext Context;
    FString Error;
    TestFalse(TEXT("ambiguous source"), LoadSource(Json(TEXT(R"({"grid":{"size":[100,100,0],"cells":[2,2,0]},"source_asset":"/Engine/BasicShapes/Cube"})")), Context, Error));
    TestFalse(TEXT("fractional grid"), LoadSource(Json(TEXT(R"({"grid":{"size":[100,100,0],"cells":[2.5,2,0]}})")), Context, Error));
    FMesh Crossing;
    Crossing.AppendVertex(FVector3d(-1,-1,0));
    Crossing.AppendVertex(FVector3d(1,-1,0));
    Crossing.AppendVertex(FVector3d(0,1,0));
    Crossing.AppendVertex(FVector3d(0,-0.5,-1));
    Crossing.AppendVertex(FVector3d(0,-0.5,1));
    Crossing.AppendVertex(FVector3d(0,0.5,0));
    Crossing.AppendTriangle(0,1,2);
    Crossing.AppendTriangle(3,4,5);
    TestFalse(TEXT("crossing rejected"), Validate(Crossing, 100, Error));
    return true;
}
#endif
