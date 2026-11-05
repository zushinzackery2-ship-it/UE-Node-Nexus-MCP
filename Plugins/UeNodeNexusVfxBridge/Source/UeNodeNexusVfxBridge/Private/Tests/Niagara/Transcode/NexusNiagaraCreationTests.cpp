#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Niagara/Compilation/NexusNiagaraCompilation.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "UeNodeNexusCollaboration.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraCreateFromText,
    "Nexus.Niagara.Transcode.CreateFromText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraCreateFromText::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/Tests/Nexus/Creation/Snow_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString AssetPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
    ON_SCOPE_EXIT
    {
        if (auto* System = FindObject<UNiagaraSystem>(nullptr, *AssetPath))
        {
            System->GetOutermost()->SetDirtyFlag(false);
            System->ClearFlags(RF_Public | RF_Standalone);
        }
    };
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("asset_path"), AssetPath);
    Request->SetBoolField(TEXT("create"), true);
    Request->SetBoolField(TEXT("compile"), false);
    Request->SetBoolField(TEXT("save"), false);
    Request->SetBoolField(TEXT("dry_run"), true);
    auto Emitter = MakeShared<FJsonObject>();
    Emitter->SetStringField(TEXT("op"), TEXT("ns_emitter_add"));
    Emitter->SetStringField(TEXT("name"), TEXT("Snow"));
    Emitter->SetStringField(TEXT("parent"), TEXT(""));
    Request->SetArrayField(TEXT("plan"), { MakeShared<FJsonValueObject>(Emitter) });
    auto Preview = HandleVfxTranscodeApply(TEXT("vfx_transcode_apply"), TEXT("niagara-create-preview"), Request);
    TestTrue(TEXT("missing system creation can be previewed"), Preview->GetBoolField(TEXT("ok")));
    TestNull(TEXT("preview leaves the asset absent"), FindObject<UNiagaraSystem>(nullptr, *AssetPath));
    Request->SetBoolField(TEXT("dry_run"), false);
    auto Created = HandleVfxTranscodeApply(TEXT("vfx_transcode_apply"), TEXT("niagara-create"), Request);
    if (!TestTrue(TEXT("text apply creates the native system and emitter"), Created->GetBoolField(TEXT("ok"))))
    {
        AddError(Created->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")));
        return false;
    }
    auto* System = FindObject<UNiagaraSystem>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("new system exists"), System))
    {
        return false;
    }
    TestTrue(TEXT("created object has asset ownership"), System->IsAsset());
    TestEqual(TEXT("authored emitter belongs to the new system"), System->GetEmitterHandles().Num(), 1);
    TestTrue(TEXT("creation is reported as changed"), Created->GetObjectField(TEXT("data"))->GetBoolField(TEXT("changed")));
    Request->SetStringField(TEXT("asset_path"), PackageName);
    Request->SetArrayField(TEXT("plan"), {});
    auto Reused = HandleVfxTranscodeApply(TEXT("vfx_transcode_apply"), TEXT("niagara-package-reuse"), Request);
    TestTrue(TEXT("package paths reuse the resident system"), Reused->GetBoolField(TEXT("ok")));
    const bool Requested = System->RequestCompile(true);
    const bool Ready = NiagaraCompilation::Wait(System);
    AddInfo(FString::Printf(TEXT("creation_compile requested=%d active=%d outstanding=%d ready=%d spawn_status=%d update_status=%d"),
        Requested, System->HasActiveCompilations(), System->HasOutstandingCompilationRequests(), Ready,
        static_cast<int32>(System->GetSystemSpawnScript()->GetLastCompileStatus()),
        static_cast<int32>(System->GetSystemUpdateScript()->GetLastCompileStatus())));
    TestTrue(TEXT("new text system with native default emitter compiles"), Ready);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraCreateRollback,
    "Nexus.Niagara.Transcode.NewAssetFailureRestoresAbsence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraCreateRollback::RunTest(const FString& Parameters)
{
    FString Error;
    FString Repository = Collaboration::BoundRepository();
    if (Repository.IsEmpty())
    {
        Repository = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tests/NexusCreation/repository"));
        if (!TestTrue(TEXT("bind owned recovery repository"), Collaboration::BindRepository(Repository, TEXT("niagara-creation"), Error)))
        {
            AddError(Error);
            return false;
        }
    }
    const FString PackageName = TEXT("/Game/Tests/Nexus/Creation/Rejected_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString AssetPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
    auto Request = MakeShared<FJsonObject>();
    Request->SetNumberField(TEXT("collaboration_version"), 1);
    Request->SetStringField(TEXT("repository"), Repository);
    Request->SetStringField(TEXT("apply_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Request->SetStringField(TEXT("asset_path"), AssetPath);
    Request->SetStringField(TEXT("kind"), TEXT("niagara_system"));
    Request->SetStringField(TEXT("asset_class"), TEXT("NiagaraSystem"));
    Request->SetBoolField(TEXT("expected_absent"), true);
    Request->SetBoolField(TEXT("create"), true);
    Request->SetBoolField(TEXT("dry_run"), false);
    Request->SetBoolField(TEXT("compile"), false);
    Request->SetBoolField(TEXT("save"), true);
    auto Invalid = MakeShared<FJsonObject>();
    Invalid->SetStringField(TEXT("op"), TEXT("ns_unknown_creation_test_verb"));
    Request->SetArrayField(TEXT("plan"), { MakeShared<FJsonValueObject>(Invalid) });
    auto Rejected = HandleVfxTranscodeApply(TEXT("vfx_transcode_apply"), TEXT("niagara-create-rollback"), Request);
    TestFalse(TEXT("invalid authored plan is rejected"), Rejected->GetBoolField(TEXT("ok")));
    const auto Data = Rejected->GetObjectField(TEXT("data"));
    const TSharedPtr<FJsonObject>* ReceiptValue = nullptr;
    if (!Data->TryGetObjectField(TEXT("receipt"), ReceiptValue) || ReceiptValue == nullptr)
    {
        AddError(TEXT("rollback did not produce a transaction receipt: ")
            + Rejected->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")));
        return false;
    }
    const auto Receipt = *ReceiptValue;
    TestEqual(TEXT("guard restores the unpublished creation"), Receipt->GetStringField(TEXT("phase")), FString(TEXT("rolled_back")));
    TestFalse(TEXT("authoritative recovery restores prior absence"), Receipt->GetObjectField(TEXT("recovered"))->GetBoolField(TEXT("exists")));
    TestFalse(TEXT("failed creation leaves no saved package"), FPackageName::DoesPackageExist(PackageName));
    return true;
}
#endif
