#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNativePawnLog,
    "Nexus.Issues7.Diagnostics.NativePawnInputError",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNativePawnLog::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::RuntimeDiagnostics;
    BeginSession(true);
    auto* Pawn = NewObject<APawn>();
    auto* Controller = NewObject<APlayerController>();
    // Expected-message filtering downgrades the native log to Verbose before
    // output devices see it. Suppress only the automation verdict instead.
    TGuardValue<bool> SuppressErrors(bSuppressLogErrors, true);
    Pawn->DisableInput(Controller);
    const auto Result = Read(MakeShared<FJsonObject>());
    TestEqual(TEXT("real native Pawn error contributes to runtime failures"), Result->GetIntegerField(TEXT("error_count")), 1);
    const auto& Events = Result->GetArrayField(TEXT("items"));
    TestTrue(TEXT("native category is retained"), Events.ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
    {
        return Value->AsObject()->GetStringField(TEXT("source")) == TEXT("LogPawn");
    }));
    EndSession();
    BeginSession(false);
    return true;
}
#endif
