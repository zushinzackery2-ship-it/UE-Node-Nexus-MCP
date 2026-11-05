#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "../../Safety/Isolation/NexusSafetyEnvironment.h"
#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeMountEvidence,
    "Nexus.Safety.RuntimeMountEvidence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeMountEvidence::RunTest(const FString& Parameters)
{
    const FString Root = TEXT("/NexusRuntimeMount_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tests/Content/"));
    FPackageName::RegisterMountPoint(Root, Directory);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(Root, Directory); };
    const auto Evidence = UeNodeNexusBridge::Safety::Environment()->GetObjectField(TEXT("project_content_mounts"));
    const FString Name = Root.Mid(1, Root.Len() - 2);
    FString Relative;
    TestTrue(TEXT("runtime registration is measured without a content plugin"), Evidence->TryGetStringField(Name, Relative));
    TestEqual(TEXT("physical directory is bound project-relatively"), Relative, FString(TEXT("Tests/Content")));
    const FString ExternalRoot = TEXT("/NexusExternal_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/");
    const FString External = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("../External/Content/"));
    FPackageName::RegisterMountPoint(ExternalRoot, External);
    ON_SCOPE_EXIT { FPackageName::UnRegisterMountPoint(ExternalRoot, External); };
    TestFalse(TEXT("external physical roots are excluded"), UeNodeNexusBridge::Safety::Environment()
        ->GetObjectField(TEXT("project_content_mounts"))->HasField(ExternalRoot.Mid(1, ExternalRoot.Len() - 2)));
    return true;
}
#endif
