#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Niagara/Compilation/NexusNiagaraCompilation.h"
#include "Niagara/Modules/UeNodeNexusBridgeNiagaraModuleStack.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusNiagaraOps.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraSaveFailure,
    "Nexus.Issues10.Niagara.SaveFailureAndUnchangedRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraSaveFailure::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge;
    auto* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
    if (!TestNotNull(TEXT("engine emitter template exists"), Template))
    {
        return false;
    }
    const FString PackageName = TEXT("/Game/NexusNiagaraSave_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Package = CreatePackage(*PackageName);
    auto* System = NewObject<UNiagaraSystem>(Package, TEXT("System"), RF_Public | RF_Standalone);
    UNiagaraSystemFactoryNew::InitializeSystem(System, true);
    FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
    const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
    ON_SCOPE_EXIT
    {
        FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
        NiagaraCompilation::Wait(System);
        ResetLoaders(Package);
        IFileManager::Get().Delete(*File);
        Package->SetDirtyFlag(false);
        System->ClearFlags(RF_Public | RF_Standalone);
    };
    auto* Output = NiagaraModuleStack::ResolveOutputNode(&System->GetEmitterHandles()[0], TEXT("ParticleUpdateScript"));
    TArray<UNiagaraNodeFunctionCall*> Modules;
    NiagaraModuleStack::GetOrderedModules(Output, Modules);
    System->RequestCompile(true);
    if (!TestTrue(TEXT("fixture has a real module"), !Modules.IsEmpty())
        || !TestTrue(TEXT("fixture is compiled and ready before saving"), NiagaraCompilation::Wait(System)))
    {
        return false;
    }
    FString Error;
    if (!TestTrue(TEXT("save fixture"), Transcode::SavePackageDirect(System, Error))
        || !TestTrue(TEXT("make fixture read-only"), FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, true)))
    {
        AddError(Error);
        return false;
    }
    TArray<uint8> Before, After;
    if (!TestTrue(TEXT("read fixture disk bytes"), FFileHelper::LoadFileToArray(Before, *File)))
    {
        return false;
    }
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("asset_path"), System->GetPathName());
    Request->SetNumberField(TEXT("emitter_index"), 0);
    Request->SetStringField(TEXT("usage"), TEXT("ParticleUpdateScript"));
    Request->SetNumberField(TEXT("module_index"), 0);
    Request->SetBoolField(TEXT("enabled"), !Modules[0]->IsNodeEnabled());
    Request->SetBoolField(TEXT("dry_run"), false);
    Request->SetBoolField(TEXT("save"), true);
    AddExpectedError(TEXT("source control checkout required"), EAutomationExpectedErrorFlags::Contains, 1);
    auto Response = HandleNiagaraModuleSetEnabled(TEXT("niagara_module_set_enabled"), TEXT("issues10-readonly"), Request);
    TestFalse(TEXT("read-only requested save returns failure"), Response->GetBoolField(TEXT("ok")));
    auto Data = Response->GetObjectField(TEXT("data"));
    TestTrue(TEXT("save was attempted"), Data->GetBoolField(TEXT("save_attempted")));
    TestTrue(TEXT("failure records partial editor state"), Data->GetBoolField(TEXT("partial")));
    TestTrue(TEXT("specific cause survives the typed response"), Data->GetStringField(TEXT("save_error")).Contains(TEXT("source control checkout required")));
    TestTrue(TEXT("read disk bytes after rejected save"), FFileHelper::LoadFileToArray(After, *File));
    TestTrue(TEXT("read-only save preserves every disk byte"), Before == After);
    FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
    Response = HandleNiagaraModuleSetEnabled(TEXT("niagara_module_set_enabled"), TEXT("issues10-retry"), Request);
    Data = Response->GetObjectField(TEXT("data"));
    TestTrue(TEXT("unchanged retry succeeds"), Response->GetBoolField(TEXT("ok")));
    TestFalse(TEXT("retry really exercises unchanged state"), Data->GetBoolField(TEXT("changed")));
    TestTrue(TEXT("dirty package is saved on unchanged retry"), Data->GetBoolField(TEXT("save_attempted")) && Data->GetBoolField(TEXT("saved")));
    return true;
}
#endif
