#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Geometry/NexusGeometry.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGeometryTerrainTest, "Nexus.Geometry.TerrainRecipe", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusGeometryTerrainTest::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Geometry;
    FObject Recipe;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"grid":{"size":[14000,34000,0],"cells":[48,116,0]},"max_triangles":100000,"ops":[{"op":"lattice","dimensions":[5,9,2],"interpolation":"cubic","offsets":[]},{"op":"remesh","target_edge_length":300,"iterations":3,"automatic":true},{"op":"noise","amplitude":25,"scale":1100,"octaves":3,"seed":29,"lock_boundary":true}]})")), Recipe);
    const TArray<double> Shore =
    {
        23500, 17700, 10800, 7100, 3600, 1700, 1300, 1600, 2400
    };
    const TArray<double> Heights =
    {
        460, 390, 280, 120, -190
    };
    TArray<TSharedPtr<FJsonValue>> Offsets;
    for (int32 I = 0; I < 5; ++I)
    {
        for (int32 J = 0; J < 9; ++J)
        {
            for (int32 K = 0; K < 2; ++K)
            {
                FObject Offset = MakeShared<FJsonObject>();
                TArray<TSharedPtr<FJsonValue>> Index, Delta;
                Index.Add(MakeShared<FJsonValueNumber>(I));
                Index.Add(MakeShared<FJsonValueNumber>(J));
                Index.Add(MakeShared<FJsonValueNumber>(K));
                Delta.Add(MakeShared<FJsonValueNumber>((Shore[J] - 7500) * FMath::Square(I / 4.0)));
                Delta.Add(MakeShared<FJsonValueNumber>(0));
                Delta.Add(MakeShared<FJsonValueNumber>(Heights[I]));
                Offset->SetArrayField(TEXT("index"), Index);
                Offset->SetArrayField(TEXT("delta"), Delta);
                Offsets.Add(MakeShared<FJsonValueObject>(Offset));
            }
        }
    }
    Recipe->GetArrayField(TEXT("ops"))[0]->AsObject()->SetArrayField(TEXT("offsets"), Offsets);
    // Exercise the curved remesh itself. A later displacement is validated
    // independently: it can intersect a tightly compressed surface.
    TArray<TSharedPtr<FJsonValue>> Operations = Recipe->GetArrayField(TEXT("ops"));
    Operations.SetNum(2);
    Recipe->SetArrayField(TEXT("ops"), Operations);
    FContext Context;
    Context.Deadline = FPlatformTime::Seconds() + 20;
    FString Error;
    if (!LoadSource(Recipe, Context, Error) || !Process(Context, Error))
    {
        AddError(Error);
        return false;
    }
    return true;
}
#endif
