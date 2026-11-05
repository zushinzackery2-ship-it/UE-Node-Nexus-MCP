#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusNiagaraOps.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraPlanReplay,
    "Nexus.Niagara.Transcode.ReplayedCreationCompiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraPlanReplay::RunTest(const FString& Parameters)
{
    FString Text;
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("UeNodeNexusVfxBridge"));
    if (!TestTrue(TEXT("VFX plugin is registered"), Plugin.IsValid()))
    {
        return false;
    }
    const FString File = Plugin->GetBaseDir() / TEXT("Config/Tests/NiagaraCreationPlan.json");
    if (!FFileHelper::LoadFileToString(Text, *File))
    {
        AddError(TEXT("Recorded authored plan is missing: ") + File);
        return false;
    }
    TSharedPtr<FJsonObject> Request;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Request) || !Request)
    {
        AddError(TEXT("Recorded plan JSON is invalid"));
        return false;
    }
    const FString Name = TEXT("Replay_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Path = TEXT("/Game/Tests/Nexus/Creation/") + Name + TEXT(".") + Name;
    Request->SetStringField(TEXT("asset_path"), Path);
    Request->SetBoolField(TEXT("create"), true);
    Request->SetBoolField(TEXT("compile"), true);
    Request->SetBoolField(TEXT("save"), true);
    Request->SetBoolField(TEXT("dry_run"), false);
    ON_SCOPE_EXIT
    {
        if (auto* System = FindObject<UNiagaraSystem>(nullptr, *Path))
        {
            System->GetOutermost()->SetDirtyFlag(false);
            System->ClearFlags(RF_Public | RF_Standalone);
        }
        const FString SavedFile = FPackageName::LongPackageNameToFilename(
            FPackageName::ObjectPathToPackageName(Path), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().Delete(*SavedFile, false, true);
    };
    const auto Result = UeNodeNexusBridge::HandleVfxTranscodeApply(TEXT("vfx_transcode_apply"), TEXT("recorded-create"), Request);
    FString Encoded;
    FJsonSerializer::Serialize(Result.ToSharedRef(), TJsonWriterFactory<>::Create(&Encoded));
    FFileHelper::SaveStringToFile(Encoded, *(FPaths::ProjectSavedDir() / TEXT("Tests/NiagaraCreation/Result.json")));
    if (!TestTrue(TEXT("recorded authored creation compiles before returning"), Result->GetBoolField(TEXT("ok"))))
    {
        AddInfo(Encoded.Left(16000));
        return false;
    }
    const auto Applied = Result->GetObjectField(TEXT("data"));
    TestEqual(TEXT("every recorded verb applied"), Applied->GetIntegerField(TEXT("applied")), Request->GetArrayField(TEXT("plan")).Num());
    TestFalse(TEXT("first compile drained its requests"), Applied->GetObjectField(TEXT("compile"))->GetBoolField(TEXT("outstanding_after_wait")));
    FString ExportError;
    const auto Raw = UeNodeNexusBridge::VfxTranscode::BuildNiagaraSystemRaw(FindObject<UNiagaraSystem>(nullptr, *Path), &ExportError);
    if (!TestTrue(TEXT("compiled creation exports its complete structure"), Raw.IsValid()))
    {
        AddError(ExportError);
        return false;
    }
    FString RawText;
    FJsonSerializer::Serialize(Raw.ToSharedRef(), TJsonWriterFactory<>::Create(&RawText));
    FFileHelper::SaveStringToFile(RawText, *(FPaths::ProjectSavedDir() / TEXT("Tests/NiagaraCreation/Raw.json")));
    int32 ExpectedModules = 0, ExpectedRenderers = 0;
    for (const auto& Entry : Request->GetArrayField(TEXT("plan")))
    {
        const FString Verb = Entry->AsObject()->GetStringField(TEXT("op"));
        ExpectedModules += Verb == TEXT("ns_module_add") ? 1 : 0;
        ExpectedRenderers += Verb == TEXT("ns_renderer_add") ? 1 : 0;
    }
    int32 ActualModules = 0, ActualRenderers = 0;
    for (const auto& Emitter : Raw->GetObjectField(TEXT("niagara"))->GetArrayField(TEXT("emitters")))
    {
        ActualRenderers += Emitter->AsObject()->GetArrayField(TEXT("renderers")).Num();
        for (const auto& Entry : Request->GetArrayField(TEXT("plan")))
        {
            const auto Operation = Entry->AsObject();
            if (Operation->GetStringField(TEXT("op")) != TEXT("ns_renderer_add")
                || Operation->GetStringField(TEXT("emitter")) != Emitter->AsObject()->GetStringField(TEXT("name")))
            {
                continue;
            }
            const FString Id = Applied->GetObjectField(TEXT("id_map"))->GetStringField(Operation->GetStringField(TEXT("id")));
            const bool Found = Emitter->AsObject()->GetArrayField(TEXT("renderers")).ContainsByPredicate([&Id](const auto& Value)
            {
                return Value->AsObject()->GetStringField(TEXT("guid")) == Id;
            });
            TestTrue(TEXT("created renderer identity survives compile and save"), Found);
        }
        for (const auto& Stack : Emitter->AsObject()->GetArrayField(TEXT("stacks")))
        {
            ActualModules += Stack->AsObject()->GetArrayField(TEXT("modules")).Num();
        }
    }
    TestEqual(TEXT("new text emitter contains exactly the authored modules"), ActualModules, ExpectedModules);
    TestEqual(TEXT("new text emitter contains exactly the authored renderers"), ActualRenderers, ExpectedRenderers);
    auto CompileRequest = MakeShared<FJsonObject>();
    CompileRequest->SetStringField(TEXT("asset_path"), Path);
    CompileRequest->SetBoolField(TEXT("wait"), true);
    const auto Recompiled = UeNodeNexusBridge::HandleNiagaraCompile(TEXT("niagara_compile"), TEXT("recorded-recompile"), CompileRequest);
    TestTrue(TEXT("public repeated compile also succeeds"), Recompiled->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("public repeated compile is ready"), Recompiled->GetObjectField(TEXT("data"))->GetBoolField(TEXT("ready_to_run")));
    return true;
}
#endif
