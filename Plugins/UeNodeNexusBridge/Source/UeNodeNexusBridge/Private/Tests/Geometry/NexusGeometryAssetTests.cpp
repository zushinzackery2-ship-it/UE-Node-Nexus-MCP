#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Geometry/NexusGeometry.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonSerializer.h"

using namespace UeNodeNexusBridge;
using namespace UeNodeNexusBridge::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGeometryAssetTest, "Nexus.Geometry.AssetRecipeAndPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusGeometryAssetTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Game/NexusGeometryTests/Mesh_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FObject Recipe;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"grid":{"size":[1000,1000,0],"cells":[12,12,0]},"ops":[{"op":"remesh","target_edge_length":70,"iterations":2},{"op":"noise","amplitude":20,"scale":500,"seed":42}]})")), Recipe);
    FObject Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("output_asset"), Path);
    Payload->SetObjectField(TEXT("recipe"), Recipe);
    Payload->SetBoolField(TEXT("dry_run"), true);
    Payload->SetBoolField(TEXT("save"), true);
    FObject Preview = HandleMeshGeometryBuild(TEXT("mesh_geometry_build"), TEXT("geometry-test-preview"), Payload);
    if (!TestTrue(TEXT("preview succeeds"), Preview->GetBoolField(TEXT("ok")))) return false;
    TestFalse(TEXT("preview did not create package file"), FPackageName::DoesPackageExist(Path));
    Payload->SetBoolField(TEXT("dry_run"), false);
    FObject Result = HandleMeshGeometryBuild(TEXT("mesh_geometry_build"), TEXT("geometry-test-create"), Payload);
    if (!Result->GetBoolField(TEXT("ok")))
    {
        AddError(Result->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")));
        return false;
    }
    TestTrue(TEXT("saved"), Result->GetObjectField(TEXT("data"))->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("saved asset file exists"), FPackageName::DoesPackageExist(Path));
    UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr, *Path);
    if (!TestNotNull(TEXT("output asset"), Asset)) return false;
    UObject* HiRes = FindObject<UObject>(Asset, TEXT("HiResMeshDescription"));
    if (!TestNotNull(TEXT("persistent HiRes source subobject"), HiRes)) return false;
    TestFalse(TEXT("HiRes source survives package save"), HiRes->HasAnyFlags(RF_Transient));
    TestTrue(TEXT("collision cooked"), Asset->GetBodySetup()->bCreatedPhysicsMeshes && !Asset->GetBodySetup()->bFailedToCreatePhysicsMeshes);
    const FString Before = Revision(Asset);
    FObject Replay = HandleMeshGeometryBuild(TEXT("mesh_geometry_build"), TEXT("geometry-test-replay"), Payload);
    TestTrue(TEXT("same recipe replay succeeds"), Replay->GetBoolField(TEXT("ok")));
    if (Replay->GetBoolField(TEXT("ok"))) TestTrue(TEXT("reused output"), Replay->GetObjectField(TEXT("data"))->GetBoolField(TEXT("reused")));
    TestEqual(TEXT("replay does not add noise twice"), Revision(Asset), Before);
    FObject Query = MakeShared<FJsonObject>();
    Query->SetStringField(TEXT("asset_path"), Path);
    const FObject Read = HandleMeshGeometryGet(TEXT("mesh_geometry_get"), TEXT("geometry-test-read"), Query);
    TestTrue(TEXT("read retains recipe"), Read->GetObjectField(TEXT("data"))->HasField(TEXT("recipe")));
    FObject Derived = MakeShared<FJsonObject>();
    Derived->SetStringField(TEXT("source_asset"), Path);
    Derived->SetStringField(TEXT("source_revision"), Before);
    Derived->SetArrayField(TEXT("ops"), {});
    Payload->SetStringField(TEXT("output_asset"), Path + TEXT("_Derived"));
    Payload->SetObjectField(TEXT("recipe"), Derived);
    const FObject Copy = HandleMeshGeometryBuild(TEXT("mesh_geometry_build"), TEXT("geometry-test-source"), Payload);
    TestTrue(TEXT("source derivative succeeds"), Copy->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("source preserved"), Revision(Asset), Before);
    Derived->SetStringField(TEXT("source_revision"), TEXT("stale"));
    const FObject Stale = HandleMeshGeometryBuild(TEXT("mesh_geometry_build"), TEXT("geometry-test-stale"), Payload);
    TestFalse(TEXT("stale source rejected"), Stale->GetBoolField(TEXT("ok")));
    return true;
}
#endif
