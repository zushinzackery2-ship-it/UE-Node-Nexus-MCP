#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InputMappingContext.h"
#include "../Fixtures/NexusPrecisionFixture.h"
#include "UeNodeNexusPropertyValue.h"
#include "Dom/JsonValue.h"
#include "Properties/NexusValueSchema.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusPropertyImportComplete,
    "Nexus.Properties.ImportCompleteValue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusPropertyImportComplete::RunTest(const FString& Parameters)
{
    auto* Context = NewObject<UInputMappingContext>();
    auto* Property = FindFProperty<FArrayProperty>(Context->GetClass(), TEXT("Mappings"));
    if (!TestNotNull(TEXT("input mappings property"), Property))
    {
        return false;
    }
    const auto Schema = UeNodeNexusBridge::Transcode::ValueSchema(Property);
    const auto Fields = Schema->GetObjectField(TEXT("element"))->GetObjectField(TEXT("fields"));
    TestFalse(TEXT("transient trigger state cannot be imported"), Fields->GetObjectField(TEXT("bHasAlwaysTickTrigger"))->GetBoolField(TEXT("text_importable")));
    TestFalse(TEXT("transient ignore state cannot be imported"), Fields->GetObjectField(TEXT("bShouldBeIgnored"))->GetBoolField(TEXT("text_importable")));
    TestTrue(TEXT("persistent input key is described"), Fields->HasField(TEXT("Key")));
    const bool Imported = UeNodeNexusBridge::ApplyPropertyText(
        Context, Property, TEXT("((Key=F10,PlayerMappableOptions=(Name=\"\")))"));
    if (!TestTrue(TEXT("native key text imports"), Imported)
        || !TestEqual(TEXT("one mapping imported"), Context->GetMappings().Num(), 1))
    {
        return false;
    }
    TestEqual(TEXT("key round trips"), Context->GetMappings()[0].Key, EKeys::F10);
    TestFalse(TEXT("trailing text is rejected"), UeNodeNexusBridge::ApplyPropertyText(
        Context, Property, TEXT("((Key=F9)) trailing")));
    TestEqual(TEXT("failed import retains array size"), Context->GetMappings().Num(), 1);
    TestEqual(TEXT("failed import retains original key"), Context->GetMappings()[0].Key, EKeys::F10);
    TestFalse(TEXT("reflected member syntax cannot replace native key syntax"), UeNodeNexusBridge::ApplyPropertyText(
        Context, Property, TEXT("((Key=(KeyName=F9)))")));
    TestEqual(TEXT("partial import retains original key"), Context->GetMappings()[0].Key, EKeys::F10);
    TestTrue(TEXT("trailing whitespace is accepted"), UeNodeNexusBridge::ApplyPropertyText(
        Context, Property, TEXT("((Key=F9)) \t")));
    TestEqual(TEXT("complete replacement commits"), Context->GetMappings()[0].Key, EKeys::F9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusPropertyImportMembers,
    "Nexus.Properties.ImportMembersAtomically",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusPropertyImportMembers::RunTest(const FString& Parameters)
{
    auto* Object = NewObject<UNexusPrecisionFixture>();
    auto* Nested = FindFProperty<FProperty>(Object->GetClass(), TEXT("Nested"));
    const float Original = Object->Nested.Tiny;
    for (const TCHAR* Invalid : { TEXT("(Tiny=2,Precise=bogus)"), TEXT("(Tiny=2,Tiny=3)"),
        TEXT("(Tiny=2,Missing=3)"), TEXT("(Tiny=2,Precise=.)"), TEXT("(Tiny=2))"),
        TEXT("(Tiny[0.5]=2)"), TEXT("(Tiny[4294967296]=2)") })
    {
        TestFalse(TEXT("invalid member rejects the complete write"),
            UeNodeNexusBridge::ApplyPropertyText(Object, Nested, Invalid));
        TestEqual(TEXT("rejected members preserve prior values"), Object->Nested.Tiny, Original);
    }
    TestTrue(TEXT("exponents and quoted delimiters import"), UeNodeNexusBridge::ApplyPropertyText(
        Object, Nested, TEXT("(Tiny=1e-12,Precise=1e3,Literal=\"a,b (c)\")")));
    TestEqual(TEXT("exponent value retained"), Object->Nested.Precise, 1000.0);
    TestEqual(TEXT("string delimiters retained"), Object->Nested.Literal, FString(TEXT("a,b (c)")));
    FString Text, Error;
    TestFalse(TEXT("JSON text uses the same validation"), UeNodeNexusBridge::ApplyPropertyJsonValue(
        Object, Nested, MakeShared<FJsonValueString>(TEXT("(Tiny=2,Precise=bogus)")), Text, Error));
    TestEqual(TEXT("JSON text rejection remains atomic"), Object->Nested.Tiny, Original);
    auto* Scalar = FindFProperty<FProperty>(Object->GetClass(), TEXT("Scalar"));
    UeNodeNexusBridge::FPropertyValueBuffer Buffer(Object, Scalar);
    TestFalse(TEXT("typed text rejects trailing junk"), Buffer.ApplyText(TEXT("2 trailing"), Error));
    TestFalse(TEXT("typed text rejects invalid number"), Buffer.ApplyText(TEXT("."), Error));
    TestTrue(TEXT("typed text accepts native float suffix"), Buffer.ApplyText(TEXT("0.5F"), Error));
    TestTrue(TEXT("typed text accepts exponent"), Buffer.ApplyText(TEXT("2e-3"), Error));
    Buffer.Commit();
    TestEqual(TEXT("typed value committed"), Object->Scalar, 0.002f);
    return true;
}
#endif
