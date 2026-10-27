#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Materials/Material.h"
#include "Transcode/Commit/NexusCommitInternal.h"
#include "UObject/Package.h"

using namespace UeNodeNexusBridge::Collaboration;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRecoveryMemory, "Nexus.Issues2.RecoveryMemory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRecoveryMemory::RunTest(const FString& Parameters)
{
    const FString Name = TEXT("/Temp/NexusRecovery_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*Name);
    UMaterial* Material = NewObject<UMaterial>(Package, TEXT("Material"), RF_Public | RF_Standalone);
    const FJson Receipt = MakeShared<FJsonObject>();
    const FJson Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("kind"), TEXT("material"));
    Request->SetStringField(TEXT("asset_path"), Material->GetPathName());
    Receipt->SetObjectField(TEXT("request"), Request);
    Receipt->SetStringField(TEXT("editor_epoch"), EditorEpoch());
    CaptureRecoveryMemory(Receipt, TEXT("package_memory_before"));
    Material->BlendMode = BLEND_Masked;
    CaptureRecoveryMemory(Receipt, TEXT("package_memory_applied"));
    Material->BlendMode = BLEND_Translucent;
    FString Error;
    TestFalse(TEXT("unrecorded save finalization is rejected"), CheckRecoveryMemory(Receipt, Error));
    CaptureRecoveryMemory(Receipt, TEXT("package_memory_saved"));
    Error.Reset();
    TestTrue(TEXT("recorded saved state can be recovered"), CheckRecoveryMemory(Receipt, Error));
    Material->TwoSided = true;
    Package->SetDirtyFlag(true);
    TestFalse(TEXT("later edits remain protected"), CheckRecoveryMemory(Receipt, Error));
    TestTrue(TEXT("rejected edits retain their dirty flag"), Package->IsDirty());
    Package->SetDirtyFlag(false);
    Material->ClearFlags(RF_Standalone | RF_Public);
    Material->MarkAsGarbage();
    return true;
}

#endif
