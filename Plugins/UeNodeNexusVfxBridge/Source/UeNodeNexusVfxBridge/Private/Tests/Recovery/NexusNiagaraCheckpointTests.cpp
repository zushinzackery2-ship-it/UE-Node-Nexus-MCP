#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Niagara/Compilation/NexusNiagaraCompilation.h"
#include "Niagara/Transcode/UeNodeNexusVfxTranscode.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraDataInterfaceArrayInt.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusCollaboration.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge;

namespace
{
TSharedPtr<FJsonObject> ReadFixture(const FString& File)
{
    FString Source;
    TSharedPtr<FJsonObject> Json;
    if (FFileHelper::LoadFileToString(Source, *File))
    {
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Source), Json);
    }
    return Json;
}

FString DiskHash(const FString& File)
{
    return LexToString(FMD5Hash::HashFile(*File));
}

bool WriteFixture(const FString& File, const TSharedPtr<FJsonObject>& Json, FString& Error)
{
    FString Source;
    if (!FJsonSerializer::Serialize(Json.ToSharedRef(), TJsonWriterFactory<>::Create(&Source))
        || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true)
        || !FFileHelper::SaveStringToFile(Source, *File))
    {
        Error = TEXT("cannot write the independent recovery fixture: ") + File;
        return false;
    }
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraCheckpoint,
    "Nexus.Issues7.Recovery.NiagaraSerializedCheckpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FNexusNiagaraCheckpoint::RunTest(const FString& Parameters)
{
    using namespace Collaboration;
    const bool bPrepare = FParse::Param(FCommandLine::Get(), TEXT("NexusColdPrepare"));
    const bool bColdRestore = FParse::Param(FCommandLine::Get(), TEXT("NexusColdRestore"));
    const FString Manifest = FPaths::ProjectSavedDir() / TEXT("Tests/NexusIssues7/NiagaraCold.json");
    FString Error;
    FString Repository = BoundRepository();
    if (Repository.IsEmpty())
    {
        Repository = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Tests/NexusIssues7/repository"));
        if (!TestTrue(TEXT("bind independent fixture repository"), BindRepository(Repository, TEXT("issues7-native"), Error)))
        {
            AddError(Error);
            return false;
        }
    }
    auto Fixture = bColdRestore ? ReadFixture(Manifest) : MakeShared<FJsonObject>();
    if (!TestTrue(TEXT("cold restore has a durable manifest"), Fixture.IsValid()))
    {
        return false;
    }
    FString CleanupPath = bColdRestore ? Fixture->GetStringField(TEXT("asset_path")) : FString();
    FString CleanupFile = bColdRestore ? Fixture->GetStringField(TEXT("file")) : FString();
    bool bKeepFixture = false;
    ON_SCOPE_EXIT
    {
        if (!bKeepFixture)
        {
            if (auto* Current = FindObject<UObject>(nullptr, *CleanupPath))
            {
                auto* Package = Current->GetOutermost();
                ResetLoaders(Package);
                Package->SetDirtyFlag(false);
                Current->ClearFlags(RF_Public | RF_Standalone);
            }
            if (!CleanupFile.IsEmpty())
            {
                IFileManager::Get().Delete(*CleanupFile);
            }
        }
    };
    if (!bColdRestore)
    {
        const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Name = TEXT("NexusNiagaraCheckpoint_") + Id;
        auto* Package = CreatePackage(*(TEXT("/Game/") + Name));
        auto* System = NewObject<UNiagaraSystem>(Package, *Name, RF_Public | RF_Standalone);
        CleanupPath = System->GetPathName();
        CleanupFile = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package->GetName(), TEXT(".uasset")));
        UNiagaraSystemFactoryNew::InitializeSystem(System, true);
        auto* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
        if (!TestNotNull(TEXT("real EmitterState template exists"), Template))
        {
            return false;
        }
        FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid);
        System->RequestCompile(true);
        if (!TestTrue(TEXT("fixture Niagara compiles"), NiagaraCompilation::Wait(System)))
        {
            return false;
        }
        const FString File = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package->GetName(), TEXT(".uasset")));
        if (!TestTrue(TEXT("save original disk"), Transcode::SavePackageTo(Package, System, File, false, Error)))
        {
            AddError(Error);
            return false;
        }
        UClass* Classes[] = { UNiagaraDataInterfaceArrayInt32::StaticClass(), UNiagaraDataInterfaceArrayFloat::StaticClass(), UNiagaraDataInterfaceArrayFloat3::StaticClass() };
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const FNiagaraVariable Variable(FNiagaraTypeDefinition(Classes[Index]),
                FName(*(FString(TEXT("User.Probe")) + FString::FromInt(Index))));
            TestTrue(TEXT("declare unsaved null user DI"), VfxTranscode::SetParameterValueText(System->GetExposedParameters(), Variable, TEXT("None"), true));
        }
        System->SetWarmupTime(0.5f);
        Package->SetDirtyFlag(true);
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("kind"), TEXT("niagara_system"));
        Request->SetStringField(TEXT("asset_path"), System->GetPathName());
        const auto Before = Observe(Request, &Error);
        if (!TestTrue(TEXT("strict user DI and EmitterState export"), Before.IsValid()))
        {
            AddError(Error);
            return false;
        }
        Request->SetStringField(TEXT("expected_revision"), Before->GetStringField(TEXT("live_revision")));
        Request->SetStringField(TEXT("apply_id"), Id);
        Request->SetStringField(TEXT("repository"), Repository);
        Request->SetNumberField(TEXT("collaboration_version"), 1);
        Request->SetBoolField(TEXT("dry_run"), false);
        Request->SetBoolField(TEXT("save"), true);
        const FString OriginalHash = DiskHash(File);
        const auto Response = RunCommit(TEXT("vfx_transcode_apply"), TEXT("niagara-checkpoint"), Request, [System](const FJson&)
        {
            System->SetWarmupTime(2.0f);
            System->MarkPackageDirty();
            auto Result = MakeEnvelope(TEXT("vfx_transcode_apply"), TEXT("niagara-checkpoint"), true);
            Result->SetObjectField(TEXT("data"), MakeShared<FJsonObject>());
            return Result;
        });
        if (!TestTrue(TEXT("apply saves a durable Niagara receipt"), Response->GetBoolField(TEXT("ok"))))
        {
            AddError(Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")));
            return false;
        }
        const auto Receipt = Response->GetObjectField(TEXT("data"))->GetObjectField(TEXT("receipt"));
        TestEqual(TEXT("precondition retains the original null interface observation"),
            Receipt->GetObjectField(TEXT("before"))->GetStringField(TEXT("content_revision")), Before->GetStringField(TEXT("content_revision")));
        TestTrue(TEXT("native load normalization is recorded separately"), Receipt->GetBoolField(TEXT("checkpoint_changed_state")));
        Fixture->SetStringField(TEXT("apply_id"), Id);
        Fixture->SetStringField(TEXT("asset_path"), System->GetPathName());
        Fixture->SetStringField(TEXT("file"), File);
        Fixture->SetStringField(TEXT("disk_hash"), OriginalHash);
        Fixture->SetStringField(TEXT("before_revision"), Before->GetStringField(TEXT("content_revision")));
        Fixture->SetStringField(TEXT("baseline_revision"), Receipt->GetObjectField(TEXT("checkpoint_baseline"))->GetStringField(TEXT("content_revision")));
        auto Rejected = MakeShared<FJsonObject>();
        Rejected->SetStringField(TEXT("id"), Id);
        Rejected->SetStringField(TEXT("asset"), System->GetPathName());
        Rejected->SetStringField(TEXT("phase"), TEXT("result_rejected"));
        Rejected->SetObjectField(TEXT("request"), Request);
        Rejected->SetObjectField(TEXT("verification_error"), MakeShared<FJsonObject>());
        if (!TestTrue(TEXT("record unpublished readback rejection"), WriteFixture(
            Repository / TEXT("transactions") / Id / TEXT("apply.json"), Rejected, Error))
            || !TestTrue(TEXT("persist cold recovery fixture"), WriteFixture(Manifest, Fixture, Error)))
        {
            AddError(Error);
            return false;
        }
        if (bPrepare)
        {
            bKeepFixture = true;
            return true;
        }
    }
    const FString Path = Fixture->GetStringField(TEXT("asset_path"));
    if (!TestNotNull(TEXT("new process can load saved Niagara"), LoadObject<UNiagaraSystem>(nullptr, *Path)))
    {
        return false;
    }
    auto Recovery = MakeShared<FJsonObject>();
    Recovery->SetStringField(TEXT("apply_id"), Fixture->GetStringField(TEXT("apply_id")));
    Recovery->SetStringField(TEXT("repository"), Repository);
    Recovery->SetBoolField(TEXT("restore"), true);
    const auto Restored = Recover(TEXT("transcode_recover"), TEXT("niagara-restore"), Recovery);
    if (!TestTrue(TEXT("formal recovery restores the serialized baseline"), Restored->GetBoolField(TEXT("ok"))))
    {
        bKeepFixture = true;
        AddError(Restored->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")));
        return false;
    }
    const auto Receipt = Restored->GetObjectField(TEXT("data"))->GetObjectField(TEXT("receipt"));
    TestEqual(TEXT("full User DI and EmitterState content revision is verified"),
        Receipt->GetObjectField(TEXT("recovered"))->GetStringField(TEXT("content_revision")), Fixture->GetStringField(TEXT("baseline_revision")));
    TestEqual(TEXT("CAS observation remains unchanged"), Receipt->GetObjectField(TEXT("before"))->GetStringField(TEXT("content_revision")), Fixture->GetStringField(TEXT("before_revision")));
    TestEqual(TEXT("original disk hash restored"), DiskHash(Fixture->GetStringField(TEXT("file"))), Fixture->GetStringField(TEXT("disk_hash")));
    TestTrue(TEXT("unsaved state survives recovery"), FindObject<UNiagaraSystem>(nullptr, *Path)->GetOutermost()->IsDirty());
    return true;
}
#endif
