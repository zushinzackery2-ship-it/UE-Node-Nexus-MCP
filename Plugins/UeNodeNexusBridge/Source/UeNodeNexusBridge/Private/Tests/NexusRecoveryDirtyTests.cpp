#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/Material.h"
#include "Transcode/Commit/NexusCommitInternal.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRecoveryDirty, "Nexus.Issues2.FailedRestorePreservesDirty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRecoveryDirty::RunTest(const FString& Parameters)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Temp/NexusDirty_") + ApplyId));
    UMaterial* Material = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone);
    Material->AddToRoot();
    Package->SetDirtyFlag(true);
    const FJson Receipt = MakeShared<FJsonObject>();
    const FJson Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("material"));
    Request->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    const FJson Row = MakeShared<FJsonObject>();
    Row->SetStringField(TEXT("package"), Package->GetName());
    Row->SetBoolField(TEXT("existed"), true);
    Row->SetBoolField(TEXT("was_dirty"), false);
    TArray<TSharedPtr<FJsonValue>> Packages;
    Packages.Add(MakeShared<FJsonValueObject>(Row));
    Receipt->SetArrayField(TEXT("packages"), Packages);
    FString Error;
    // Incomplete checkpoint metadata must fail before replacing memory or files.
    TestFalse(TEXT("incomplete checkpoint cannot be restored"), RestoreCheckpoint(Receipt, Error));
    TestTrue(TEXT("failure includes an explanation"), !Error.IsEmpty());
    TestTrue(TEXT("failed restore retains the applied dirty state"), Package->IsDirty());
    SaveReceipt(Receipt, TEXT("rejected"), Error);
    Material->RemoveFromRoot();
    Material->ClearFlags(RF_Standalone | RF_Public);
    Material->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    return true;
}

#endif
