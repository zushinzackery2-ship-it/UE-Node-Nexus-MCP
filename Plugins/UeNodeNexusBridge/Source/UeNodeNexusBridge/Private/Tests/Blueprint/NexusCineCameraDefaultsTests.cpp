#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UeNodeNexusBridgeTranscode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusCineCameraDefaults,
    "Nexus.Blueprint.CineCameraDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusCineCameraDefaults::RunTest(const FString& Parameters)
{
    UClass* Parent = LoadClass<AActor>(nullptr, TEXT("/Script/CinematicCamera.CineCameraActor"));
    if (Parent == nullptr)
    {
        AddInfo(TEXT("CinematicCamera is not enabled in this test project; skipped"));
        return true;
    }
    const FName Name = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("NexusCineCamera"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent, GetTransientPackage(), Name,
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (!TestNotNull(TEXT("camera blueprint was created"), Blueprint))
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const auto Raw = UeNodeNexusBridge::Transcode::BuildBlueprintRaw(Blueprint);
    const auto& Components = Raw->GetObjectField(TEXT("blueprint"))->GetArrayField(TEXT("components"));
    TSharedPtr<FJsonObject> Camera;
    for (const auto& Value : Components)
    {
        const auto Row = Value->AsObject();
        if (Row->GetStringField(TEXT("name")) == TEXT("CameraComponent"))
        {
            Camera = Row;
            break;
        }
    }
    if (!TestTrue(TEXT("inherited camera component is exported"), Camera.IsValid()))
    {
        return false;
    }
    bool bSawAspectRatio = false;
    for (const auto& Value : Camera->GetArrayField(TEXT("props")))
    {
        const auto Prop = Value->AsObject();
        const FString PropName = Prop->GetStringField(TEXT("name"));
        TestFalse(TEXT("derived FieldOfView is not a writable prop"), PropName == TEXT("FieldOfView"));
        if (PropName == TEXT("AspectRatio"))
        {
            bSawAspectRatio = true;
            TestEqual(TEXT("inherited camera default is the parent CDO value"),
                      Prop->GetStringField(TEXT("default")), Prop->GetStringField(TEXT("value")));
        }
    }
    TestTrue(TEXT("AspectRatio is exported"), bSawAspectRatio);
    return true;
}
#endif
