#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Diagnostics/UeNodeNexusBridgeDiagnostics.h"
#include "Logging/MessageLog.h"
#include "Animation/AnimBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusRuntimeVisibility,
    "Nexus.Diagnostics.Runtime.MessageLogVisibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusRuntimeVisibility::RunTest(const FString& Parameters)
{
    UeNodeNexusBridge::RuntimeDiagnostics::BeginSession(true);
    const FString Marker = TEXT("Nexus regression MotionState null");
    for (int32 Index = 0; Index < 6; ++Index)
    {
        FMessageLog Log(TEXT("PIE"));
        Log.SuppressLoggingToOutputLog();
        Log.Error(FText::FromString(Marker));
    }
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("severity"), TEXT("error"));
    auto Result = UeNodeNexusBridge::CollectBridgeDiagnostics(Payload);
    int32 Count = 0;
    for (const auto& Value : Result.Diagnostics)
    {
        auto Item = Value->AsObject();
        if (Item->GetStringField(TEXT("message")).Contains(Marker))
        {
            Count += Item->GetIntegerField(TEXT("occurrence_count"));
        }
    }
    TestEqual(TEXT("all six PIE messages remain visible with output mirroring suppressed"), Count, 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAnimDiagnosticVisibility,
    "Nexus.Diagnostics.Runtime.AnimBlueprintCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAnimDiagnosticVisibility::RunTest(const FString& Parameters)
{
    auto Package = CreatePackage(TEXT("/Game/NexusRegression/AnimDiagnostic"));
    auto Blueprint = NewObject<UAnimBlueprint>(Package, TEXT("AnimDiagnostic"), RF_Public | RF_Standalone);
    Blueprint->Status = BS_UpToDate;
    FAssetRegistryModule::AssetCreated(Blueprint);
    auto Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    auto Result = UeNodeNexusBridge::CollectBridgeDiagnostics(Payload);
    TestEqual(TEXT("loaded AnimBlueprint reuses Blueprint diagnostics"), Result.AssetsSupported, 1);
    TestEqual(TEXT("AnimBlueprint is not classified unsupported"), Result.AssetsUnsupported, 0);
    FAssetRegistryModule::AssetDeleted(Blueprint);
    Blueprint->ClearFlags(RF_Public | RF_Standalone);
    return true;
}
#endif
