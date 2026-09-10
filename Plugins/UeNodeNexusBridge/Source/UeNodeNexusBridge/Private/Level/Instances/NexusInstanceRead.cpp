#include "NexusInstanceEdit.h"

#include "NexusInstanceIdentity.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Misc/SecureHash.h"

namespace UeNodeNexusBridge::Instances
{
bool Export(UInstancedStaticMeshComponent* Component, Scene::FObject& Out, FString& Error, bool bRebind, int32 Offset, int32 Limit)
{
    TArray<FGuid> Ids;
    const bool bReadOnly = IsConstructionOwned(Component);
    if (!ReadIds(Component, Ids, Error, bRebind || bReadOnly))
    {
        return false;
    }
    if (Offset < 0 || Offset > Ids.Num() || Limit < 1
        || static_cast<int64>(Component->NumCustomDataFloats) * Ids.Num() != Component->PerInstanceSMCustomData.Num())
    {
        Error = TEXT("invalid_instance_page_or_custom_data_buffer");
        return false;
    }
    Scene::FRows Items;
    const int32 End = Offset + FMath::Min(Limit, Ids.Num() - Offset);
    Items.Reserve(End - Offset);
    for (int32 Index = Offset; Index < End; ++Index)
    {
        FTransform Transform;
        if (!Component->GetInstanceTransform(Index, Transform, false) || Transform.ContainsNaN())
        {
            Error = TEXT("instance_read_failed");
            return false;
        }
        Scene::FObject Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("id"), Ids[Index].ToString(EGuidFormats::Digits));
        Item->SetStringField(TEXT("transform"), Scene::TransformText(Transform));
        Scene::FRows Custom;
        for (int32 Channel = 0; Channel < Component->NumCustomDataFloats; ++Channel)
        {
            const int32 DataOffset = Index * Component->NumCustomDataFloats + Channel;
            if (!Component->PerInstanceSMCustomData.IsValidIndex(DataOffset) || !FMath::IsFinite(Component->PerInstanceSMCustomData[DataOffset]))
            {
                Error = TEXT("invalid_custom_data_buffer");
                return false;
            }
            Custom.Add(MakeShared<FJsonValueNumber>(Component->PerInstanceSMCustomData[DataOffset]));
        }
        Item->SetArrayField(TEXT("custom_data"), Custom);
        Items.Add(MakeShared<FJsonValueObject>(Item));
    }
    Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("custom_data_count"), Component->NumCustomDataFloats);
    Out->SetArrayField(TEXT("instances"), Items);
    Out->SetNumberField(TEXT("total"), Ids.Num());
    Out->SetNumberField(TEXT("next_offset"), End < Ids.Num() ? End : -1);
    Out->SetBoolField(TEXT("read_only"), bReadOnly);
    Out->SetBoolField(TEXT("identity_bound"), IsIdentityBound(Component));
    const FString Content = ContentRevision(Component);
    FMD5 Hash;
    const FTCHARToUTF8 Bytes(*Content);
    Hash.Update(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
    Hash.Update(reinterpret_cast<const uint8*>(Ids.GetData()), Ids.Num() * sizeof(FGuid));
    uint8 Result[16];
    Hash.Final(Result);
    Out->SetStringField(TEXT("revision"), BytesToHex(Result, 16));
    return true;
}

bool Parse(const Scene::FRows& Rows, int32 CustomCount, TArray<FInstance>& Out, FString& Error)
{
    if (CustomCount < 0 || CustomCount > 1024)
    {
        Error = TEXT("invalid_custom_data_count: expected 0..1024");
        return false;
    }
    TSet<FGuid> Seen;
    Out.Reserve(Rows.Num());
    for (const auto& Value : Rows)
    {
        const Scene::FObject* Json = nullptr;
        FInstance Item;
        if (!Value.IsValid() || !Value->TryGetObject(Json) || !FGuid::Parse(Scene::String(*Json, TEXT("id")), Item.Id)
            || !Item.Id.IsValid() || Seen.Contains(Item.Id)
            || !Scene::ReadTransform(Scene::String(*Json, TEXT("transform")), Item.Transform, Error))
        {
            Error = TEXT("invalid_instance: unique id and valid transform are required");
            return false;
        }
        Seen.Add(Item.Id);
        const auto& Custom = Scene::Rows(*Json, TEXT("custom_data"));
        if (Custom.Num() != CustomCount)
        {
            Error = TEXT("custom_data_size_mismatch");
            return false;
        }
        for (const auto& Number : Custom)
        {
            double Scalar;
            if (!Number.IsValid() || !Number->TryGetNumber(Scalar) || !FMath::IsFinite(Scalar)
                || FMath::Abs(Scalar) > MAX_flt)
            {
                Error = TEXT("invalid_custom_data: finite float values are required");
                return false;
            }
            Item.CustomData.Add(static_cast<float>(Scalar));
        }
        Out.Add(MoveTemp(Item));
    }
    return true;
}
}
