#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "K2Node_IfThenElse.h"
#include "Transcode/Blueprint/NexusBlueprintPinNames.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusIfThenElseAliases,
    "Nexus.Blueprint.IfThenElseAliases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusIfThenElseAliases::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Transcode;
    const TArray<FPinAlias> Aliases = PinAliasesForClass(UK2Node_IfThenElse::StaticClass());
    TSet<FString> Names;
    for (const FPinAlias& Alias : Aliases)
    {
        Names.Add(FString(Alias.Alias) + TEXT("->") + Alias.PinName);
    }
    TestTrue(TEXT("True maps to then"), Names.Contains(TEXT("True->then")));
    TestTrue(TEXT("False maps to else"), Names.Contains(TEXT("False->else")));
    return true;
}
#endif
