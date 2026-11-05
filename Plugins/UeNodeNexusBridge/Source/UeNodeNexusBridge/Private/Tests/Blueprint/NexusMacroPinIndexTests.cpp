#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Transcode/Schema/NexusSchema.h"

using namespace UeNodeNexusBridge::Transcode;

namespace
{
bool HasPin(const TSharedPtr<FJsonObject>& Record, const TCHAR* Name, const TCHAR* Direction)
{
    const TArray<TSharedPtr<FJsonValue>>* Pins = nullptr;
    if (!Record.IsValid() || !Record->TryGetArrayField(TEXT("pins"), Pins) || Pins == nullptr)
    {
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Pins)
    {
        const TSharedPtr<FJsonObject> Pin = Value.IsValid() ? Value->AsObject() : nullptr;
        FString PinName;
        FString PinDirection;
        if (Pin.IsValid()
            && Pin->TryGetStringField(TEXT("name"), PinName)
            && Pin->TryGetStringField(TEXT("dir"), PinDirection)
            && PinName == Name && PinDirection == Direction)
        {
            return true;
        }
    }
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusMacroTunnelIndex,
    "Nexus.Issues22.MacroTunnelIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusMacroTunnelIndex::RunTest(const FString& Parameters)
{
    const TSharedPtr<FJsonObject> Macros = MacroIndex();
    const TSharedPtr<FJsonObject> ForLoop = Macros->GetObjectField(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros.ForLoop"));
    if (!TestNotNull(TEXT("the engine standard macro library is indexed"), ForLoop.Get()))
    {
        return false;
    }
    // ForLoop's entry tunnel is spelled ``execute`` while ForEachLoop's is
    // ``Exec``; the index must carry each graph's own spelling, not a guess.
    TestTrue(TEXT("ForLoop accepts its declared entry"), HasPin(ForLoop, TEXT("execute"), TEXT("in")));
    TestFalse(TEXT("ForLoop does not invent Exec"), HasPin(ForLoop, TEXT("Exec"), TEXT("in")));
    TestTrue(TEXT("ForLoop exposes LoopBody"), HasPin(ForLoop, TEXT("LoopBody"), TEXT("out")));
    const TSharedPtr<FJsonObject> ForEach = Macros->GetObjectField(TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros.ForEachLoop"));
    if (TestNotNull(TEXT("ForEachLoop is indexed too"), ForEach.Get()))
    {
        TestTrue(TEXT("ForEachLoop keeps its own entry spelling"), HasPin(ForEach, TEXT("Exec"), TEXT("in")));
    }
    return true;
}

#endif
