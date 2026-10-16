#include "Animation/NexusSkeletonAuthoringLibrary.h"

#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "NexusSkeletonMaskJson.h"
#include "ScopedTransaction.h"

using namespace UeNodeNexusBridge::Animation;

DEFINE_LOG_CATEGORY_STATIC(LogNexusSkeletonAuthoring, Log, All);

static FString Reject(const FString& Error)
{
    UE_LOG(LogNexusSkeletonAuthoring, Warning, TEXT("event=mask_rejected reason=%s"), *Error);
    return MaskResult(false, Error);
}

FString UNexusSkeletonAuthoringLibrary::WriteBlendMaskJson(USkeleton* Skeleton, FName ProfileName,
    const FString& WeightsJson, bool ReplaceExisting, bool DryRun)
{
    TArray<FMaskWeight> Weights;
    FString Error;
    if (!ValidateMaskWeights(Skeleton, ProfileName, WeightsJson, Weights, Error))
    {
        return Reject(Error);
    }
    UBlendProfile* Profile = Skeleton->GetBlendProfile(ProfileName);
    if (Profile && (!ReplaceExisting || !Profile->IsBlendMask()))
    {
        return Reject(TEXT("An existing profile requires explicit replacement and BlendMask mode"));
    }
    if (!Profile && FindObject<UObject>(Skeleton, *ProfileName.ToString()))
    {
        return Reject(TEXT("A skeleton child object already owns the requested profile name"));
    }
    auto Preview = MakeShared<FJsonObject>();
    Preview->SetBoolField(TEXT("dry_run"), DryRun);
    Preview->SetNumberField(TEXT("bones"), Weights.Num());
    if (DryRun)
    {
        return MaskResult(true, FString(), Preview);
    }
    const FScopedTransaction Transaction(NSLOCTEXT("Nexus", "WriteBlendMask", "Write native skeleton blend mask"));
    Skeleton->Modify();
    if (!Profile)
    {
        Profile = Skeleton->CreateNewBlendProfile(ProfileName);
    }
    if (!Profile)
    {
        return Reject(TEXT("Native skeleton profile creation failed"));
    }
    Profile->Modify();
    Profile->Mode = EBlendProfileMode::BlendMask;
    Profile->ProfileEntries.Reset();
    for (const auto& Weight : Weights)
    {
        Profile->SetBoneBlendScale(Weight.BoneIndex, Weight.Weight, false, true);
    }
    Profile->RefreshBoneEntriesFromName();
    Profile->MarkPackageDirty();
    auto Result = ReadMask(Skeleton, Profile);
    Result->SetBoolField(TEXT("dry_run"), false);
    UE_LOG(LogNexusSkeletonAuthoring, Log, TEXT("event=mask_written skeleton=%s profile=%s bones=%d"),
        *Skeleton->GetPathName(), *ProfileName.ToString(), Weights.Num());
    return MaskResult(true, FString(), Result);
}

FString UNexusSkeletonAuthoringLibrary::ReadBlendMaskJson(USkeleton* Skeleton, FName ProfileName)
{
    if (!IsInGameThread() || !IsValid(Skeleton))
    {
        return Reject(TEXT("A valid skeleton is required on the editor game thread"));
    }
    UBlendProfile* Profile = Skeleton->GetBlendProfile(ProfileName);
    if (!Profile)
    {
        return Reject(TEXT("The requested blend profile does not exist"));
    }
    return MaskResult(true, FString(), ReadMask(Skeleton, Profile));
}
