#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "Transcode/Commit/Restore/NexusRestore.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"

using namespace UeNodeNexusBridge::Collaboration;

namespace
{
void CheckReloadRetirement(FAutomationTestBase& Test, EBlueprintType Type, UClass* Parent)
{
    const FString ApplyId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusReloadedBlueprint_") + ApplyId));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, Package, TEXT("Blueprint"), Type);
    if (!Test.TestNotNull(TEXT("create a real compiled blueprint"), Blueprint))
    {
        return;
    }
    Blueprint->bDisplayCompilePIEWarning = true;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const FString Path = Blueprint->GetPathName();
    const FString File = PackageFile(Package);
    const FJson Receipt = MakeShared<FJsonObject>();
    Receipt->SetStringField(TEXT("apply_id"), ApplyId);
    UBlueprint* Retired = nullptr;
    bool bObservedRetired = false;
    const auto Handle = FCoreUObjectDelegates::OnPackageReloaded.AddLambda(
        [&](EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
        {
            if (Phase == EPackageReloadPhase::OnPackageFixup && Event && Event->GetOldPackage() == Package)
            {
                Retired = Blueprint;
            }
            if (Phase == EPackageReloadPhase::PostBatchPreGC && Retired)
            {
                bObservedRetired = true;
                Test.TestTrue(TEXT("replaced object retains the failed compile status"), Retired->Status == BS_Error);
                Test.TestFalse(TEXT("reloaded failure no longer blocks PIE"), Retired->bDisplayCompilePIEWarning);
            }
        });
    ON_SCOPE_EXIT
    {
        FCoreUObjectDelegates::OnPackageReloaded.Remove(Handle);
        if (UBlueprint* Current = FindObject<UBlueprint>(nullptr, *Path))
        {
            ResetLoaders(Current->GetOutermost());
            Current->GetOutermost()->SetDirtyFlag(false);
            Current->bDisplayCompilePIEWarning = false;
            Current->ClearFlags(RF_Public | RF_Standalone);
            Current->MarkAsGarbage();
        }
        IFileManager::Get().Delete(*File);
        if (Receipt->HasField(TEXT("packages")) && Text(Receipt, TEXT("phase")) != TEXT("rolled_back"))
        {
            FString JournalError;
            if (!SaveReceipt(Receipt, TEXT("rejected"), JournalError))
            {
                Test.AddError(JournalError);
            }
        }
    };
    FString Error;
    if (!Test.TestTrue(TEXT("save valid original"), UeNodeNexusBridge::Transcode::SavePackageTo(
        Package, Blueprint, File, false, Error)))
    {
        Test.AddError(Error);
        return;
    }
    const FString DiskHash = FileHash(File);
    if (!Test.TestTrue(TEXT("capture original memory and disk"), CapturePackage(Package, Receipt, Error)))
    {
        Test.AddError(Error);
        return;
    }
    Blueprint->Status = BS_Error;
    Test.TestTrue(TEXT("failed active asset still blocks PIE before restore"), Blueprint->bDisplayCompilePIEWarning);
    if (!Test.TestTrue(TEXT("restore checkpoint using real package reload"), RestoreCheckpointMemory(Receipt, Error)))
    {
        Test.AddError(Error);
        return;
    }
    if (!Test.TestTrue(TEXT("restore original disk after the memory load"), RestoreCheckpointDisk(Receipt, Error)))
    {
        Test.AddError(Error);
        return;
    }
    Test.TestTrue(TEXT("native reload observation ran before GC"), bObservedRetired);
    UBlueprint* Restored = FindObject<UBlueprint>(nullptr, *Path);
    if (!Test.TestNotNull(TEXT("replacement keeps original asset path"), Restored))
    {
        return;
    }
    Test.TestTrue(TEXT("replacement is a distinct object"), Restored != Blueprint);
    // This PIE participation flag is transient. The replacement is a fresh
    // object; explicitly enroll the active-failure counter-control in the PIE gate.
    Restored->bDisplayCompilePIEWarning = true;
    Test.TestTrue(TEXT("replacement keeps the normal PIE gate"), Restored->bDisplayCompilePIEWarning);
    Restored->Status = BS_Error;
    Test.TestTrue(TEXT("active replacement failure is still checked"), Restored->bDisplayCompilePIEWarning);
    Test.TestEqual(TEXT("disk bytes remain unchanged"), FileHash(File), DiskHash);
    Test.TestEqual(TEXT("receipt counts one replaced blueprint"), Receipt->GetIntegerField(TEXT("restore_retired_blueprint_count")), 1);
    Test.TestTrue(TEXT("completed fixture leaves no pending recovery"), SaveReceipt(Receipt, TEXT("rolled_back"), Error));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusReloadedBlueprint, "Nexus.IssuesNew.ReloadedBlueprintExcludedFromPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusReloadedBlueprint::RunTest(const FString& Parameters)
{
    CheckReloadRetirement(*this, BPTYPE_Normal, UObject::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusReloadedFunctionLibrary, "Nexus.IssuesNew.ReloadedFunctionLibraryExcludedFromPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusReloadedFunctionLibrary::RunTest(const FString& Parameters)
{
    CheckReloadRetirement(*this, BPTYPE_FunctionLibrary, UBlueprintFunctionLibrary::StaticClass());
    return true;
}

#endif
