#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Diagnostics/NexusViewportDiagnosticsLibrary.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Diagnostics/Viewport/NexusViewportPixelProbe.h"
#include "HAL/PlatformProcess.h"
#include "RenderCommandFence.h"
#include "RenderingThread.h"

static TSharedPtr<FJsonObject> ViewportJson(const FString& Text)
{
    TSharedPtr<FJsonObject> Result;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result);
    return Result;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusViewportReflection,
    "Nexus.Viewport.DiagnosticsReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusViewportReflection::RunTest(const FString& Parameters)
{
    UClass* Library = UNexusViewportDiagnosticsLibrary::StaticClass();
    const TCHAR* Names[] =
    {
        TEXT("PrimaryViewportStateJson"), TEXT("SetPrimaryViewportInputEnabledJson"),
        TEXT("ResetPrimaryViewportHistoryJson"), TEXT("CapturePrimaryAtPhaseJson"),
        TEXT("CapturePrimaryAtShaderPhaseJson"), TEXT("QueuePrimaryPixelProbeJson"),
        TEXT("ReadPrimaryPixelProbeJson"), TEXT("ProfilePrimaryGpu")
    };
    for (const TCHAR* Name : Names)
    {
        TestNotNull(Name, Library->FindFunctionByName(FName(Name)));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusViewportInvalidPhase,
    "Nexus.Viewport.InvalidPhaseAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusViewportInvalidPhase::RunTest(const FString& Parameters)
{
    const FString Invalid[] =
    {
        UNexusViewportDiagnosticsLibrary::CapturePrimaryAtPhaseJson(TEXT("../escape"), 0, 0, 10),
        UNexusViewportDiagnosticsLibrary::CapturePrimaryAtPhaseJson(TEXT("valid"), -1, 0, 10),
        UNexusViewportDiagnosticsLibrary::CapturePrimaryAtShaderPhaseJson(TEXT("valid"), 0, 8, 10),
        UNexusViewportDiagnosticsLibrary::CapturePrimaryAtPhaseJson(TEXT("valid"), 0, 0, 0.5f)
    };
    for (const FString& Text : Invalid)
    {
        const auto Result = ViewportJson(Text);
        if (!TestTrue(TEXT("structured rejection"), Result.IsValid()))
        {
            continue;
        }
        TestFalse(TEXT("no screenshot queued"), Result->GetBoolField(TEXT("requested")));
        TestEqual(TEXT("reason"), Result->GetStringField(TEXT("error_code")), FString(TEXT("invalid_request")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusViewportWrongThread,
    "Nexus.Viewport.GameThreadOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusViewportWrongThread::RunTest(const FString& Parameters)
{
    const FString Text = Async(EAsyncExecution::Thread, []()
    {
        return UNexusViewportDiagnosticsLibrary::PrimaryViewportStateJson();
    }).Get();
    const auto Result = ViewportJson(Text);
    TestTrue(TEXT("worker returns a structured observation"), Result.IsValid());
    if (Result.IsValid())
    {
        TestFalse(TEXT("viewport access requires game thread"), Result->GetBoolField(TEXT("available")));
    }
    const auto ProbeResult = ViewportJson(Async(EAsyncExecution::Thread, []()
    {
        return UNexusViewportDiagnosticsLibrary::ReadPrimaryPixelProbeJson();
    }).Get());
    TestTrue(TEXT("worker probe returns a rejection"), ProbeResult.IsValid());
    if (ProbeResult.IsValid())
    {
        TestEqual(TEXT("probe reason"), ProbeResult->GetStringField(TEXT("error_code")), FString(TEXT("game_thread_required")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusViewportProbeTimeout,
    "Nexus.Viewport.PixelProbeDeadline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusViewportProbeTimeout::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Diagnostics;
    const FIntRect Rect(0, 0, 4, 4);
    TestTrue(TEXT("unmatched probe admitted"), QueueViewportPixelProbe(Rect, 37, MAX_int32, 0, 0.01));
    TestFalse(TEXT("parallel probe rejected"), QueueViewportPixelProbe(Rect, 37, 0, 0, 1));
    FPlatformProcess::Sleep(0.03f);
    const auto Result = ReadViewportPixelProbe();
    TestEqual(TEXT("terminal timeout"), Result->GetStringField(TEXT("state")), FString(TEXT("failed")));
    TestEqual(TEXT("timeout reason"), Result->GetStringField(TEXT("error_code")), FString(TEXT("pixel_probe_timeout")));
    TestTrue(TEXT("timeout releases admission"), QueueViewportPixelProbe(Rect, 37, 0, 0, 1));
    ENQUEUE_RENDER_COMMAND(NexusProbeTestCleanup)
    ([](FRHICommandListImmediate&)
    {
        StopViewportPixelProbe_RenderThread();
    });
    FRenderCommandFence Fence;
    Fence.BeginFence();
    Fence.Wait(false);
    return true;
}
#endif
