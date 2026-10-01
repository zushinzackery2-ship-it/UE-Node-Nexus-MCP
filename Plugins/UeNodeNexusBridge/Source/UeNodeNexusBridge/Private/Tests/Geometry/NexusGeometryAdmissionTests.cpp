#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CoreGlobals.h"
#include "Serialization/JsonSerializer.h"
#include "Transport/UeNodeNexusBridgeRequestDispatch.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusGeometryAdmissionTest, "Nexus.Geometry.StreamingAdmission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNexusGeometryAdmissionTest::RunTest(const FString& Parameters)
{
    const FString Path = TEXT("/Game/NexusGeometryTests/Admission_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TSharedPtr<FJsonObject> Request;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TEXT(R"({"operation":"mesh_geometry_build","request_id":"geometry-admission","payload":{"recipe":{"grid":{"size":[100,100,0],"cells":[2,2,0]},"ops":[]},"dry_run":false,"save":true}})")), Request);
    Request->GetObjectField(TEXT("payload"))->SetStringField(TEXT("output_asset"), Path);
    SuspendTextureStreamingRenderTasks();
    const FString Blocked = UeNodeNexusBridge::DispatchParsedRequest(Request);
    ResumeTextureStreamingRenderTasks();
    TSharedPtr<FJsonObject> Response;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Blocked), Response);
    TestFalse(TEXT("suspended request rejected"), Response->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("standard busy retry"), Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("bridge_busy")));
    TestNull(TEXT("no package mutation before admission"), FindPackage(nullptr, *Path));
    const FString Accepted = UeNodeNexusBridge::DispatchParsedRequest(Request);
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Accepted), Response);
    TestTrue(TEXT("request saves once owner resumes"), Response->GetBoolField(TEXT("ok")));
    return true;
}
#endif
