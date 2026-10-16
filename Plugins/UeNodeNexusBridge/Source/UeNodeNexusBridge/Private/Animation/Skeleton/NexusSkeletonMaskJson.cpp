#include "NexusSkeletonMaskJson.h"

#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Animation
{
bool ValidateMaskWeights(USkeleton* Skeleton, FName Name, const FString& Json,
    TArray<FMaskWeight>& Weights, FString& Error)
{
    if (!IsInGameThread() || !IsValid(Skeleton) || Name.IsNone())
    {
        Error = TEXT("A valid skeleton and profile name are required on the editor game thread");
        return false;
    }
    TArray<TSharedPtr<FJsonValue>> Rows;
    const auto Reader = TJsonReaderFactory<>::Create(Json);
    const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
    if (Reference.GetNum() == 0 || !FJsonSerializer::Deserialize(Reader, Rows) || Rows.Num() != Reference.GetNum())
    {
        Error = TEXT("Weights must be a JSON array with one explicit entry for every reference bone");
        return false;
    }
    TSet<int32> Seen;
    for (const auto& Value : Rows)
    {
        const TSharedPtr<FJsonObject>* Row = nullptr;
        FString Bone;
        double Weight = 0.0;
        if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row || !Row->IsValid()
            || !(*Row)->TryGetStringField(TEXT("bone"), Bone)
            || !(*Row)->TryGetNumberField(TEXT("scale"), Weight))
        {
            Error = TEXT("Every bone entry requires a bone name and numeric scale");
            return false;
        }
        const int32 Index = Reference.FindBoneIndex(FName(*Bone));
        if (Index == INDEX_NONE || Seen.Contains(Index)
            || !FMath::IsFinite(Weight) || Weight < 0.0 || Weight > 1.0)
        {
            Error = FString::Printf(TEXT("Invalid, duplicate or out-of-range bone entry: %s"), *Bone);
            return false;
        }
        Seen.Add(Index);
        FMaskWeight Entry;
        Entry.BoneIndex = Index;
        Entry.Weight = static_cast<float>(Weight);
        Weights.Add(Entry);
    }
    return true;
}

TSharedPtr<FJsonObject> ReadMask(USkeleton* Skeleton, UBlendProfile* Profile)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("skeleton"), Skeleton->GetPathName());
    Result->SetStringField(TEXT("profile"), Profile->GetPathName());
    Result->SetBoolField(TEXT("is_blend_mask"), Profile->IsBlendMask());
    Result->SetBoolField(TEXT("owning_skeleton_matches"), Profile->OwningSkeleton == Skeleton);
    TArray<TSharedPtr<FJsonValue>> Rows;
    const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
    for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("bone"), Reference.GetBoneName(Index).ToString());
        Row->SetNumberField(TEXT("index"), Index);
        Row->SetNumberField(TEXT("scale"), Profile->GetBoneBlendScale(Index));
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    Result->SetArrayField(TEXT("weights"), Rows);
    bool CacheValid = true;
    for (const auto& Entry : Profile->ProfileEntries)
    {
        CacheValid &= Entry.BoneReference.BoneIndex == Reference.FindBoneIndex(Entry.BoneReference.BoneName)
            && Entry.BoneReference.BoneIndex != INDEX_NONE && Entry.BoneReference.bUseSkeletonIndex;
    }
    Result->SetBoolField(TEXT("native_bone_cache_valid"), CacheValid);
    Result->SetNumberField(TEXT("stored_entries"), Profile->GetNumBlendEntries());
    return Result;
}

FString MaskResult(bool Success, const FString& Error, const TSharedPtr<FJsonObject>& Data)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("ok"), Success);
    if (!Error.IsEmpty())
    {
        Result->SetStringField(TEXT("error"), Error);
    }
    if (Data.IsValid())
    {
        Result->SetObjectField(TEXT("data"), Data);
    }
    return SerializeJsonObjectToString(Result);
}
}
