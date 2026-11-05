#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Niagara/Transcode/Values/NexusNiagaraValue.h"

using namespace UeNodeNexusBridge::VfxTranscode;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusNiagaraStrictNumbers,
    "Nexus.Niagara.Transcode.StrictNumbers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusNiagaraStrictNumbers::RunTest(const FString& Parameters)
{
    for (const TCHAR* Text : { TEXT("."), TEXT("e"), TEXT("1e"), TEXT("1e+"), TEXT("+"),
        TEXT("nan"), TEXT("inf"), TEXT("1e999"), TEXT("2.5tail"), TEXT(".f"), TEXT("1e+f") })
    {
        FParsedValue Parsed;
        FString Error;
        TestFalse(FString(TEXT("invalid scalar rejected: ")) + Text,
            ParseParameterValue(FNiagaraTypeDefinition::GetFloatDef(), Text, Parsed, Error));
        TestFalse(TEXT("invalid value reports its reason"), Error.IsEmpty());
    }
    FParsedValue Parsed;
    FString Error;
    TestFalse(TEXT("named vector rejects an incomplete exponent"),
        ParseParameterValue(FNiagaraTypeDefinition::GetVec3Def(), TEXT("(X=1,Y=2,Z=1e+)"), Parsed, Error));
    for (const TCHAR* Text : { TEXT("0"), TEXT(" -2.5 "), TEXT(".5"), TEXT("1."), TEXT("0.5f"), TEXT("0.5F"), TEXT("1.25e+2") })
    {
        Error.Reset();
        TestTrue(FString(TEXT("valid decimal accepted: ")) + Text,
            ParseParameterValue(FNiagaraTypeDefinition::GetFloatDef(), Text, Parsed, Error));
    }
    TestTrue(TEXT("scientific notation has its actual value"), Parsed.Components[0] == 125.0);
    return true;
}
#endif
