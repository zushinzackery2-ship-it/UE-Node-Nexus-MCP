#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeRequestDispatch.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSaveAdmission,
    "Nexus.Issues6.Transport.SaveAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSaveAdmission::RunTest(const FString& Parameters)
{
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("request_id"), TEXT("save-admission-regression"));
    // An unknown operation is harmless on the unfixed dispatcher, but must also
    // be rejected before dispatch while object serialization owns the thread.
    Request->SetStringField(TEXT("operation"), TEXT("nexus_save_admission_probe"));
    Request->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
    FString Body;
    {
        TGuardValue<bool> Saving(GIsSavingPackage, true);
        TestTrue(TEXT("engine saving predicate active"), UE::IsSavingPackage());
        Body = UeNodeNexusBridge::DispatchParsedRequest(Request);
    }
    TSharedPtr<FJsonObject> Response;
    if (TestTrue(TEXT("structured response"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Response)))
    {
        TestEqual(TEXT("save phase prevents dispatch"), Response->GetObjectField(TEXT("error"))->GetStringField(TEXT("code")), FString(TEXT("bridge_busy")));
    }
    TestFalse(TEXT("request ownership released"), UeNodeNexusBridge::IsBridgeRequestActive());
    return true;
}

#endif
