#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AssetRegistry/AssetRegistryModule.h"
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Transcode/Commit/NexusPackageFiles.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace
{
UMaterialFunction* MakeFunction(const FString& PackageName)
{
    auto* Function = NewObject<UMaterialFunction>(CreatePackage(*PackageName), TEXT("Function"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Function);
    return Function;
}

template <typename T>
T* Expression(UMaterialFunction* Function)
{
    return CastChecked<T>(UMaterialEditingLibrary::CreateMaterialExpressionInFunction(Function, T::StaticClass()));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFunctionDeleteCheckpoints,
    "Nexus.Issues8.Material.DependentDeleteCheckpoints",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFunctionDeleteCheckpoints::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Collaboration;
    FString Error;
    if (BoundRepository().IsEmpty() && !BindRepository(
        FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("NexusIssues8Repository")),
        FGuid::NewGuid().ToString(EGuidFormats::Digits), Error))
    {
        AddError(Error);
        return false;
    }
    const FString Prefix = TEXT("/Game/NexusIssues8Delete_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto* Child = MakeFunction(Prefix + TEXT("_Child"));
    auto* Parent = MakeFunction(Prefix + TEXT("_Parent"));
    const FString ChildPath = Child->GetPathName();
    const FString ParentPath = Parent->GetPathName();
    auto* Value = Expression<UMaterialExpressionConstant>(Child);
    Value->R = 0.25f;
    Expression<UMaterialExpressionFunctionOutput>(Child)->A.Connect(0, Value);
    auto* Call = Expression<UMaterialExpressionMaterialFunctionCall>(Parent);
    Call->SetMaterialFunction(Child);
    Expression<UMaterialExpressionFunctionOutput>(Parent)->A.Connect(0, Call);
    const FString Paths[]
    {
        ParentPath, ChildPath
    };
    ON_SCOPE_EXIT
    {
        for (const FString& Path : Paths)
        {
            if (auto* Asset = FindObject<UObject>(nullptr, *Path))
            {
                auto* Package = Asset->GetOutermost();
                ResetLoaders(Package);
                IFileManager::Get().Delete(*PackageFile(Package));
                Package->SetDirtyFlag(false);
                Asset->ClearFlags(RF_Public | RF_Standalone);
                Asset->MarkAsGarbage();
            }
        }
    };
    UMaterialFunction* Functions[]
    {
        Child, Parent
    };
    for (auto* Function : Functions)
    {
        if (!UeNodeNexusBridge::Transcode::SavePackageTo(
            Function->GetOutermost(), Function, PackageFile(Function->GetOutermost()), false, Error))
        {
            AddError(Error);
            return false;
        }
    }
    // Deliver the fixture's initial save notifications before marking its
    // packages deleted, as an editor does between separate user operations.
    FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")).Get()->Tick(0.0f);
    FAssetRegistryModule::GetRegistry().Tick(0.0f);
    FAssetRegistryModule::GetRegistry().WaitForCompletion();
    for (const FString& Path : Paths)
    {
        auto Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("asset_path"), Path);
        Request->SetStringField(TEXT("kind"), TEXT("material_function"));
        Request->SetBoolField(TEXT("delete_asset"), true);
        Request->SetBoolField(TEXT("dry_run"), false);
        Request->SetBoolField(TEXT("save"), true);
        Request->SetStringField(TEXT("apply_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Request->SetNumberField(TEXT("collaboration_version"), 1);
        Request->SetStringField(TEXT("repository"), BoundRepository());
        const auto Before = Observe(Request, &Error);
        if (!TestTrue(TEXT("Live function observation is available"), Before.IsValid()))
        {
            AddError(Error);
            return false;
        }
        Request->SetStringField(TEXT("expected_revision"), Text(Before, TEXT("live_revision")));
        const auto Result = RunCommit(TEXT("transcode_apply"), TEXT("issues8-delete"), Request,
            [](const FJson& Payload)
            {
                return DeleteAsset(TEXT("transcode_apply"), TEXT("issues8-delete"), Payload);
            });
        if (!TestTrue(TEXT("Checkpoint and protected deletion complete"), Flag(Result, TEXT("ok"))))
        {
            AddError(Text(Object(Result, TEXT("error")), TEXT("message")));
            return false;
        }
        const auto Receipt = Object(Object(Result, TEXT("data")), TEXT("receipt"));
        TestEqual(TEXT("Deletion has a durable committed receipt"), Text(Receipt, TEXT("phase")), FString(TEXT("ue_committed")));
        TestFalse(TEXT("Deleted function is absent in the saved result"), Flag(Object(Receipt, TEXT("after")), TEXT("exists")));
        for (const auto& Package : Rows(Receipt, TEXT("packages")))
        {
            for (const auto& File : Rows(Package->AsObject(), TEXT("files")))
            {
                TestEqual(TEXT("Every deleted package file is absent"),
                    Text(File->AsObject(), TEXT("saved_hash")), FString(TEXT("absent")));
            }
        }
        TestNotNull(TEXT("Snapshot checkpoint remains present"), Object(Receipt, TEXT("checkpoint_baseline")).Get());
    }
    return true;
}
#endif
