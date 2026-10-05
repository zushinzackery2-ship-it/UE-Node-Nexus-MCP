#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Async/TaskGraphInterfaces.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Transport/Dispatch/NexusFrameQueue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFrameQueueBoundary,
    "Nexus.Safety.Dispatch.WorldTickBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFrameQueueBoundary::RunTest(const FString& Parameters)
{
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("operation"), TEXT("bridge_capabilities_get"));
    Request->SetStringField(TEXT("request_id"), TEXT("frame-boundary-regression"));
    Request->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
    auto Future = UeNodeNexusBridge::EnqueueBridgeRequest(Request);
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
    TestFalse(TEXT("pumping game-thread tasks cannot execute a queued bridge request"), Future.IsReady());
    UeNodeNexusBridge::PumpRequestQueue();
    TestTrue(TEXT("the explicit frame boundary executes the queued request"), Future.IsReady());
    if (Future.IsReady())
    {
        TSharedPtr<FJsonObject> Response;
        const FString Text = Future.Get();
        const bool bParsed = FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Response);
        TestTrue(TEXT("the dispatched request returns its normal response"), bParsed && Response->GetBoolField(TEXT("ok")));
    }
    return true;
}
#endif
