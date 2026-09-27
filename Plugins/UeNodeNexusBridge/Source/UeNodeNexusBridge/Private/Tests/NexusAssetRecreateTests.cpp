#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "ScopedTransaction.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeAssetCreateHelpers.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

using namespace UeNodeNexusBridge;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCreatePreview, "Nexus.Issues2.RecreatePreviewPreservesUndo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCreatePreview::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UMaterial> Unrelated(NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transactional));
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Unrelated user edit")));
        Unrelated->Modify();
        Unrelated->TwoSided = true;
    }
    const int32 UndoCount = GEditor->Trans->GetQueueLength();
    TestTrue(TEXT("unrelated edit is present in undo history"), UndoCount > 0);
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusRecreate_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UMaterial> Deleted(NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone));
    Deleted->TwoSided = true;
    Deleted->ClearFlags(RF_Public | RF_Standalone);
    const FString Path = Deleted->GetPathName();
    const auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), Path);
    Payload->SetStringField(TEXT("asset_kind"), TEXT("material"));
    Payload->SetBoolField(TEXT("dry_run"), true);
    const auto Preview = HandleAssetCreate(TEXT("asset_create"), TEXT("preview"), Payload);
    TestTrue(TEXT("preview permits recreating a deleted asset"), Preview->GetBoolField(TEXT("ok")));
    TestTrue(TEXT("preview leaves the old object in place"), FindObject<UObject>(nullptr, *Path) == Deleted.Get());
    TestEqual(TEXT("preview preserves unrelated undo history"), GEditor->Trans->GetQueueLength(), UndoCount);
    Payload->SetBoolField(TEXT("dry_run"), false);
    const auto Created = HandleAssetCreate(TEXT("asset_create"), TEXT("create"), Payload);
    TestTrue(TEXT("creation succeeds"), Created->GetBoolField(TEXT("ok")));
    UMaterial* Replacement = FindObject<UMaterial>(nullptr, *Path);
    TestTrue(TEXT("new asset has its own identity"), Replacement && Replacement != Deleted.Get() && Replacement->IsAsset());
    TestTrue(TEXT("old external reference stays valid"), IsValid(Deleted.Get()) && Deleted->GetOuter() == GetTransientPackage());
    TestEqual(TEXT("creation preserves unrelated undo history"), GEditor->Trans->GetQueueLength(), UndoCount);
    if (Replacement)
    {
        TestFalse(TEXT("new asset receives fresh defaults"), Replacement->TwoSided != GetDefault<UMaterial>()->TwoSided);
        FAssetRegistryModule::AssetDeleted(Replacement);
        Replacement->ClearFlags(RF_Public | RF_Standalone);
        Replacement->MarkAsGarbage();
    }
    Package->SetDirtyFlag(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusTranscodeRecreate, "Nexus.Issues2.TranscodeRecreatesDeletedAsset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusTranscodeRecreate::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(*(TEXT("/Game/NexusRecreateMf_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    TStrongObjectPtr<UMaterialFunction> Deleted(NewObject<UMaterialFunction>(Package, TEXT("Function"), RF_Public | RF_Standalone));
    Deleted->ClearFlags(RF_Public | RF_Standalone);
    const FString Path = Deleted->GetPathName();
    const auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), Path);
    Payload->SetStringField(TEXT("kind"), TEXT("material_function"));
    Payload->SetBoolField(TEXT("create"), true);
    Payload->SetBoolField(TEXT("dry_run"), true);
    Payload->SetBoolField(TEXT("compile"), false);
    Payload->SetBoolField(TEXT("save"), false);
    Payload->SetArrayField(TEXT("plan"), TArray<TSharedPtr<FJsonValue>>());
    const auto Preview = HandleTranscodeApply(TEXT("transcode_apply"), TEXT("preview"), Payload);
    bool bWouldCreate = false;
    Preview->GetObjectField(TEXT("data"))->TryGetBoolField(TEXT("would_create"), bWouldCreate);
    TestTrue(TEXT("preview treats a retained deleted object as absent"), bWouldCreate);
    TestTrue(TEXT("preview leaves the retained object untouched"), FindObject<UObject>(nullptr, *Path) == Deleted.Get());
    Payload->SetBoolField(TEXT("dry_run"), false);
    const auto Applied = HandleTranscodeApply(TEXT("transcode_apply"), TEXT("create"), Payload);
    TestTrue(TEXT("apply creates a new asset"), Applied->GetBoolField(TEXT("ok")) && Applied->GetObjectField(TEXT("data"))->GetBoolField(TEXT("created")));
    UObject* Replacement = FindObject<UObject>(nullptr, *Path);
    TestTrue(TEXT("apply does not edit the deleted object"), Replacement && Replacement != Deleted.Get() && Replacement->IsAsset());
    if (Replacement)
    {
        FAssetRegistryModule::AssetDeleted(Replacement);
        Replacement->ClearFlags(RF_Public | RF_Standalone);
        Replacement->MarkAsGarbage();
    }
    Package->SetDirtyFlag(false);
    return true;
}

#endif
