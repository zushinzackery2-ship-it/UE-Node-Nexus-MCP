#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "GameFramework/Info.h"
#include "Transcode/Schema/NexusSchema.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusAbstractSchemaMetadata,
    "Nexus.ContentStructure.Schema.AbstractParentMetadata",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusAbstractSchemaMetadata::RunTest(const FString& Parameters)
{
    const auto Record = MakeShared<FJsonObject>();
    UeNodeNexusBridge::Transcode::AddClassMetadata(AInfo::StaticClass(), Record);
    TestTrue(TEXT("abstract parent is identified for reflection"), Record->GetBoolField(TEXT("abstract")));
    TestEqual(TEXT("parent retains the inheritance edge"), Record->GetStringField(TEXT("inheritance")), FString(TEXT("/Script/Engine.Actor")));
    TestFalse(TEXT("abstract parent cannot be instantiated"), Record->GetObjectField(TEXT("bridge"))->GetBoolField(TEXT("create")));
    return true;
}
#endif
