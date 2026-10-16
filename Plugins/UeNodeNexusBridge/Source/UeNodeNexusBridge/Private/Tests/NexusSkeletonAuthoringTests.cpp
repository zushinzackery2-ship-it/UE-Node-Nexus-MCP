#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/NexusSkeletonAuthoringLibrary.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
USkeleton* TestSkeleton()
{
    auto* Skeleton = NewObject<USkeleton>();
    FReferenceSkeletonModifier Modifier(const_cast<FReferenceSkeleton&>(Skeleton->GetReferenceSkeleton()), Skeleton);
    Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("hip"), TEXT("hip"), 0), FTransform::Identity);
    Modifier.Add(FMeshBoneInfo(TEXT("head"), TEXT("head"), 1), FTransform::Identity);
    return Skeleton;
}

bool Successful(const FString& Text)
{
    TSharedPtr<FJsonObject> Value;
    return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value)
        && Value.IsValid() && Value->GetBoolField(TEXT("ok"));
}

const FString Original = TEXT("[{\"bone\":\"root\",\"scale\":1},{\"bone\":\"hip\",\"scale\":0.25},{\"bone\":\"head\",\"scale\":0}]");
const FString Changed = TEXT("[{\"bone\":\"root\",\"scale\":0},{\"bone\":\"hip\",\"scale\":0.75},{\"bone\":\"head\",\"scale\":1}]");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSkeletonMaskAuthoring,
    "Nexus.Animation.SkeletonMask.Authoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSkeletonMaskAuthoring::RunTest(const FString& Parameters)
{
    auto* Skeleton = TestSkeleton();
    const FName Name(TEXT("SourceMask"));
    TestTrue(TEXT("dry run accepts exact bones"), Successful(
        UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Original, false, true)));
    TestNull(TEXT("dry run creates no profile"), Skeleton->GetBlendProfile(Name));
    TestTrue(TEXT("native profile authoring succeeds"), Successful(
        UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Original, false, false)));
    UBlendProfile* Profile = Skeleton->GetBlendProfile(Name);
    if (!TestNotNull(TEXT("profile belongs to skeleton"), Profile))
    {
        return false;
    }
    TestTrue(TEXT("native mode is BlendMask"), Profile->IsBlendMask());
    TestEqual(TEXT("root weight"), Profile->GetBoneBlendScale(0), 1.0f);
    TestEqual(TEXT("fractional hip weight"), Profile->GetBoneBlendScale(1), 0.25f);
    TestEqual(TEXT("zero head weight"), Profile->GetBoneBlendScale(2), 0.0f);
    AddExpectedError(TEXT("event=mask_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("implicit replacement rejected"), Successful(
        UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Changed, false, false)));
    TestEqual(TEXT("rejection preserves fractional weight"), Profile->GetBoneBlendScale(1), 0.25f);
    TestTrue(TEXT("explicit replacement succeeds"), Successful(
        UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Changed, true, false)));
    TestEqual(TEXT("profile identity retained"), Skeleton->GetBlendProfile(Name), Profile);
    TestEqual(TEXT("replacement has no stale root entry"), Profile->GetBoneBlendScale(0), 0.0f);
    TestEqual(TEXT("replacement fraction"), Profile->GetBoneBlendScale(1), 0.75f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSkeletonMaskValidation,
    "Nexus.Animation.SkeletonMask.Validation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSkeletonMaskValidation::RunTest(const FString& Parameters)
{
    auto* Skeleton = TestSkeleton();
    const FName Name(TEXT("SourceMask"));
    AddExpectedError(TEXT("event=mask_rejected"), EAutomationExpectedErrorFlags::Contains, 5);
    const TArray<FString> Invalid =
    {
        TEXT("[]"),
        TEXT("[{\"bone\":\"root\",\"scale\":1},{\"bone\":\"root\",\"scale\":1},{\"bone\":\"head\",\"scale\":0}]"),
        TEXT("[{\"bone\":\"root\",\"scale\":1},{\"bone\":\"missing\",\"scale\":1},{\"bone\":\"head\",\"scale\":0}]"),
        TEXT("[{\"bone\":\"root\",\"scale\":1},{\"bone\":\"hip\",\"scale\":1.1},{\"bone\":\"head\",\"scale\":0}]")
    };
    for (const auto& Value : Invalid)
    {
        TestFalse(TEXT("invalid input rejected atomically"), Successful(
            UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Value, false, false)));
        TestEqual(TEXT("invalid input leaves profile collection unchanged"), Skeleton->BlendProfiles.Num(), 0);
    }
    TestFalse(TEXT("missing profile read is explicit"), Successful(
        UNexusSkeletonAuthoringLibrary::ReadBlendMaskJson(Skeleton, Name)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNexusSkeletonMaskCache,
    "Nexus.Animation.SkeletonMask.NativeBoneCache",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNexusSkeletonMaskCache::RunTest(const FString& Parameters)
{
    auto* Skeleton = TestSkeleton();
    const FName Name(TEXT("SourceMask"));
    TestTrue(TEXT("author source mask"), Successful(
        UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(Skeleton, Name, Original, false, false)));
    UBlendProfile* Profile = Skeleton->GetBlendProfile(Name);
    if (!Profile)
    {
        return false;
    }
    for (auto& Entry : Profile->ProfileEntries)
    {
        Entry.BoneReference.InvalidateCachedBoneIndex();
    }
    Profile->PostLoad();
    TestEqual(TEXT("PostLoad rebuilds index based lookup"), Profile->GetBoneBlendScale(1), 0.25f);
    TestEqual(TEXT("PostLoad preserves zero default"), Profile->GetBoneBlendScale(2), 0.0f);
    TSharedPtr<FJsonObject> Result;
    const FString Readback = UNexusSkeletonAuthoringLibrary::ReadBlendMaskJson(Skeleton, Name);
    TestTrue(TEXT("native readback deserializes"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Readback), Result));
    if (Result.IsValid())
    {
        TestTrue(TEXT("native bone caches verified"), Result->GetObjectField(TEXT("data"))->GetBoolField(TEXT("native_bone_cache_valid")));
    }
    return true;
}

#endif
