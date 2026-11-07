#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UeNodeNexusBridgeTranscode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusStructDefaultContract,
    "Nexus.Blueprint.Defaults.ReflectedStructWriteContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusStructDefaultContract::RunTest(const FString& Parameters)
{
    auto* Blueprint = FKismetEditorUtilities::CreateBlueprint(AActor::StaticClass(), GetTransientPackage(),
        MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusStructDefaults")),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const auto Raw = UeNodeNexusBridge::Transcode::BuildBlueprintRaw(Blueprint);
    const auto& Defaults = Raw->GetObjectField(TEXT("blueprint"))->GetArrayField(TEXT("defaults"));
    const auto* Tick = Defaults.FindByPredicate([](const auto& Item)
    {
        return Item->AsObject()->GetStringField(TEXT("name")) == TEXT("PrimaryActorTick");
    });
    if (!TestNotNull(TEXT("native tick default is exported"), Tick))
    {
        return false;
    }
    const TSharedPtr<FJsonObject>* Contract = nullptr;
    if (!TestTrue(TEXT("native default supplies its typed write contract"),
        (*Tick)->AsObject()->TryGetObjectField(TEXT("value_schema"), Contract)))
    {
        return false;
    }
    TestEqual(TEXT("contract is reflected struct"), (*Contract)->GetStringField(TEXT("kind")), FString(TEXT("struct")));
    const auto Fields = (*Contract)->GetObjectField(TEXT("fields"));
    TestTrue(TEXT("actual bool member is described"), Fields->HasField(TEXT("bCanEverTick")));
    TestTrue(TEXT("actual float member is described"), Fields->HasField(TEXT("TickInterval")));
    return !HasAnyErrors();
}
#endif
