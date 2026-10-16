#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Materials/Material.h"
#include "Misc/Guid.h"
#include "Safety/Isolation/NexusSafetyBaseline.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusEmptyBaseline,
    "Nexus.Safety.EmptyBaseline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusEmptyBaseline::RunTest(const FString& Parameters)
{
    const FString Name = TEXT("/Game/NexusBaselineTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*Name);
    Package->MarkAsFullyLoaded();
    auto Export = [&Name]()
    {
        auto Payload = MakeShared<FJsonObject>();
        TArray<TSharedPtr<FJsonValue>> Paths;
        Paths.Add(MakeShared<FJsonValueString>(Name));
        Payload->SetArrayField(TEXT("asset_paths"), Paths);
        Payload->SetBoolField(TEXT("dry_run"), false);
        Payload->SetStringField(TEXT("baseline_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
        return UeNodeNexusBridge::Safety::ExportBaseline(TEXT("safety_baseline_export"), TEXT("empty-baseline-test"), Payload);
    };
    auto Empty = Export();
    if (!TestTrue(TEXT("empty package export succeeds"), Empty->GetBoolField(TEXT("ok"))))
    {
        return false;
    }
    const auto& EmptyFiles = Empty->GetObjectField(TEXT("data"))->GetArrayField(TEXT("files"));
    TestEqual(TEXT("absence preserves every package sidecar"), EmptyFiles.Num(), 5);
    for (const auto& File : EmptyFiles)
    {
        TestEqual(TEXT("empty package stays absent"), File->AsObject()->GetStringField(TEXT("hash")), FString(TEXT("absent")));
    }
    UMaterial* Material = NewObject<UMaterial>(Package, TEXT("UnsavedAsset"), RF_Public | RF_Standalone);
    Package->MarkPackageDirty();
    auto Present = Export();
    TestTrue(TEXT("unsaved real asset export succeeds"), Present->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("unsaved dirty state survives"), Package->IsDirty());
    if (Present->GetBoolField(TEXT("ok")))
    {
        const auto& Files = Present->GetObjectField(TEXT("data"))->GetArrayField(TEXT("files"));
        TestNotEqual(TEXT("real asset has serialized bytes"), Files[0]->AsObject()->GetStringField(TEXT("hash")), FString(TEXT("absent")));
    }
    Material->ClearFlags(RF_Standalone | RF_Public);
    Material->MarkAsGarbage();
    return true;
}
#endif
