#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Engine/Scene.h"
#include "Transcode/Properties/NexusValueSchema.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusFixedArraySchema,
    "Nexus.Schema.FixedArrayDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusFixedArraySchema::RunTest(const FString& Parameters)
{
    using namespace UeNodeNexusBridge::Transcode;
    UScriptStruct* Struct = FPostProcessSettings::StaticStruct();
    FProperty* Property = FindFProperty<FProperty>(Struct, TEXT("LensFlareTints"));
    if (!TestNotNull(TEXT("native fixed color array"), Property))
    {
        return false;
    }
    const auto Fields = StructValueSchema(Struct)->GetObjectField(TEXT("fields"));
    TestEqual(TEXT("native array has eight elements"), Property->ArrayDim, 8);
    TestFalse(TEXT("unindexed scalar default is absent"), Fields->HasField(TEXT("LensFlareTints")));
    FPostProcessSettings Defaults;
    TSet<FString> Distinct;
    for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
    {
        const FString Name = FString::Printf(TEXT("LensFlareTints[%d]"), Index);
        const TSharedPtr<FJsonObject>* Field = nullptr;
        if (!TestTrue(TEXT("each indexed contract exists"), Fields->TryGetObjectField(Name, Field)))
        {
            return false;
        }
        FString Native;
        Property->ExportTextItem_Direct(Native, Property->ContainerPtrToValuePtr<void>(&Defaults, Index),
            nullptr, nullptr, PPF_None);
        TestEqual(TEXT("default is the actual indexed native color"), (*Field)->GetStringField(TEXT("default")), Native);
        TestEqual(TEXT("contract records element index"), (*Field)->GetIntegerField(TEXT("array_index")), Index);
        TestEqual(TEXT("contract records fixed size"), (*Field)->GetIntegerField(TEXT("array_dim")), 8);
        Distinct.Add(Native);
    }
    TestTrue(TEXT("native defaults are not repeated element zero"), Distinct.Num() > 1);
    const auto Blend = StructValueSchema(FWeightedBlendables::StaticStruct())->GetObjectField(TEXT("fields"))->GetObjectField(TEXT("Array"));
    TestEqual(TEXT("empty member retains array contract"), Blend->GetStringField(TEXT("kind")), FString(TEXT("array")));
    TestEqual(TEXT("native empty array export is retained"), Blend->GetStringField(TEXT("default")), FString());
    return true;
}
#endif
