#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Blueprint.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UeNodeNexusBridgeAssetCreateHelpers.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

using namespace UeNodeNexusBridge;

namespace
{
void CheckRetirement(FAutomationTestBase& Test, EBlueprintType Type, UClass* Parent)
{
    const FString PackageName = TEXT("/Game/NexusRetiredBlueprint_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*PackageName);
    TStrongObjectPtr<UBlueprint> Failed(NewObject<UBlueprint>(Package, TEXT("Failed"), RF_Public | RF_Standalone));
    Failed->BlueprintType = Type;
    Failed->ParentClass = Parent;
    Failed->Status = BS_Error;
    Failed->bDisplayCompilePIEWarning = true;
    const FString Path = Failed->GetPathName();

    Test.TestFalse(TEXT("active failed Blueprint cannot be retired"), ReleaseDiscardedAssetObject(Path));
    Test.TestTrue(TEXT("active failure remains visible to PIE"), Failed->bDisplayCompilePIEWarning);
    Test.TestTrue(TEXT("active object retains its identity"), Failed->GetPathName() == Path);
    Test.TestTrue(TEXT("active error status is preserved"), Failed->Status == BS_Error);

    Failed->ClearFlags(RF_Public | RF_Standalone);
    const bool bRetired = ReleaseDiscardedAssetObject(Path);
    Test.TestTrue(TEXT("discarded failed Blueprint is retired"), bRetired);
    Test.TestTrue(TEXT("old external reference stays valid"), IsValid(Failed.Get()));
    Test.TestTrue(TEXT("retired object moves to Transient"), Failed->GetOuter() == GetTransientPackage());
    Test.TestTrue(TEXT("retired object keeps the original compile error"), Failed->Status == BS_Error);
    Test.TestFalse(TEXT("retired compile error is excluded from PIE"), Failed->bDisplayCompilePIEWarning);
    Test.TestTrue(TEXT("retirement frees the original path"), FindObject<UObject>(nullptr, *Path) == nullptr);

    if (bRetired)
    {
        TStrongObjectPtr<UBlueprint> Replacement(NewObject<UBlueprint>(Package, TEXT("Failed"), RF_Public | RF_Standalone));
        Replacement->BlueprintType = Type;
        Replacement->ParentClass = Parent;
        Replacement->Status = BS_Error;
        Replacement->bDisplayCompilePIEWarning = true;
        Test.TestTrue(TEXT("replacement has a separate identity"), Replacement.Get() != Failed.Get());
        Test.TestTrue(TEXT("replacement failure remains visible to PIE"), Replacement->bDisplayCompilePIEWarning);
        Test.TestFalse(TEXT("replacement active asset cannot be retired"), ReleaseDiscardedAssetObject(Path));
        Test.TestTrue(TEXT("replacement active error status is preserved"), Replacement->Status == BS_Error);
        Replacement->ClearFlags(RF_Public | RF_Standalone);
        Test.TestTrue(TEXT("fixture replacement is retired during cleanup"), ReleaseDiscardedAssetObject(Path));
        // A failing regression must not leave its own error in the editor's PIE list.
        Replacement->bDisplayCompilePIEWarning = false;
        Replacement->MarkAsGarbage();
    }
    Failed->bDisplayCompilePIEWarning = false;
    Failed->MarkAsGarbage();
    Package->SetDirtyFlag(false);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRetiredBlueprint, "Nexus.IssuesNew.RetiredBlueprintExcludedFromPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRetiredBlueprint::RunTest(const FString& Parameters)
{
    CheckRetirement(*this, BPTYPE_Normal, UObject::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRetiredFunctionLibrary, "Nexus.IssuesNew.RetiredFunctionLibraryExcludedFromPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRetiredFunctionLibrary::RunTest(const FString& Parameters)
{
    CheckRetirement(*this, BPTYPE_FunctionLibrary, UBlueprintFunctionLibrary::StaticClass());
    return true;
}

#endif
