#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraSystem.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusVfxPresence, "Nexus.Issues2.DeletedNiagaraObservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusVfxPresence::RunTest(const FString& Parameters)
{
    const FString PreviousRoot = Transcode::GetMirrorRoot();
    const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NexusVfxPresence"));
    IFileManager::Get().MakeDirectory(*Root, true);
    FString Error;
    if (!TestTrue(TEXT("configure isolated export directory"), Transcode::SetMirrorRoot(Root, Error)))
    {
        return false;
    }
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusVfxPresence_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UNiagaraSystem* System = NewObject<UNiagaraSystem>(Package, TEXT("System"), RF_Public | RF_Standalone);
    System->ClearFlags(RF_Public | RF_Standalone);
    TestTrue(TEXT("deleted system remains loaded"), LoadObject<UObject>(nullptr, *System->GetPathName()) == System);
    const auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("out_dir"), Root);
    TArray<TSharedPtr<FJsonValue>> Paths;
    Paths.Add(MakeShared<FJsonValueString>(System->GetPathName()));
    Payload->SetArrayField(TEXT("asset_paths"), Paths);
    const auto Data = HandleVfxTranscodeExport(TEXT("vfx_transcode_export"), TEXT("presence"), Payload)->GetObjectField(TEXT("data"));
    TestEqual(TEXT("export emits no deleted Niagara system"), Data->GetArrayField(TEXT("assets")).Num(), 0);
    const auto& Skipped = Data->GetArrayField(TEXT("skipped"));
    if (TestEqual(TEXT("export reports missing system"), Skipped.Num(), 1))
    {
        TestEqual(TEXT("deleted system is absent, not an unsupported type"), Skipped[0]->AsObject()->GetStringField(TEXT("reason")), FString(TEXT("asset_not_found")));
    }
    System->MarkAsGarbage();
    Package->SetDirtyFlag(false);
    if (!PreviousRoot.IsEmpty())
    {
        Transcode::SetMirrorRoot(PreviousRoot, Error);
    }
    return true;
}

#endif
