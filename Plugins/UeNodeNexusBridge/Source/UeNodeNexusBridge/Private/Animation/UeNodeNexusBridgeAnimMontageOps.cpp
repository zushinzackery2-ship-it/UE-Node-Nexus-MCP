#include "UeNodeNexusBridgeOperations.h"

#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Dom/JsonValue.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString PathOrEmpty(const UObject* Object)
{
    return Object ? Object->GetPathName() : FString();
}

TSharedPtr<FJsonValue> MakeSectionRow(const UAnimMontage* Montage, int32 Index)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Montage->GetSectionName(Index).ToString()));
    Row.Add(MakeShared<FJsonValueNumber>(Montage->CompositeSections[Index].GetTime()));
    Row.Add(MakeShared<FJsonValueNumber>(Montage->GetSectionLength(Index)));
    Row.Add(MakeShared<FJsonValueString>(Montage->CompositeSections[Index].NextSectionName.ToString()));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeSectionObject(const UAnimMontage* Montage, int32 Index)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("section_name"), Montage->GetSectionName(Index).ToString());
    Json->SetNumberField(TEXT("start_time"), Montage->CompositeSections[Index].GetTime());
    Json->SetNumberField(TEXT("length"), Montage->GetSectionLength(Index));
    Json->SetStringField(TEXT("next_section"), Montage->CompositeSections[Index].NextSectionName.ToString());
    return Json;
}

TSharedPtr<FJsonValue> MakeSegmentRow(const FString& SlotName, const FAnimSegment& Segment)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(SlotName));
    Row.Add(MakeShared<FJsonValueString>(PathOrEmpty(Segment.GetAnimReference())));
    Row.Add(MakeShared<FJsonValueNumber>(Segment.StartPos));
    Row.Add(MakeShared<FJsonValueNumber>(Segment.AnimStartTime));
    Row.Add(MakeShared<FJsonValueNumber>(Segment.AnimEndTime));
    Row.Add(MakeShared<FJsonValueNumber>(Segment.AnimPlayRate));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonValue> MakeSegmentObject(const FString& SlotName, const FAnimSegment& Segment)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("slot"), SlotName);
    Json->SetStringField(TEXT("animation"), PathOrEmpty(Segment.GetAnimReference()));
    Json->SetNumberField(TEXT("start_pos"), Segment.StartPos);
    Json->SetNumberField(TEXT("anim_start_time"), Segment.AnimStartTime);
    Json->SetNumberField(TEXT("anim_end_time"), Segment.AnimEndTime);
    Json->SetNumberField(TEXT("play_rate"), Segment.AnimPlayRate);
    Json->SetNumberField(TEXT("loop_count"), Segment.LoopingCount);
    return MakeShared<FJsonValueObject>(Json);
}

FString NotifyClassName(const FAnimNotifyEvent& Event)
{
    if (Event.NotifyStateClass != nullptr)
    {
        return Event.NotifyStateClass->GetClass()->GetName();
    }
    if (Event.Notify != nullptr)
    {
        return Event.Notify->GetClass()->GetName();
    }
    return FString();
}

TSharedPtr<FJsonValue> MakeNotifyRow(const FAnimNotifyEvent& Event)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Event.NotifyName.ToString()));
    Row.Add(MakeShared<FJsonValueNumber>(Event.GetTriggerTime()));
    Row.Add(MakeShared<FJsonValueNumber>(Event.GetDuration()));
    Row.Add(MakeShared<FJsonValueNumber>(Event.TrackIndex));
    Row.Add(MakeShared<FJsonValueString>(NotifyClassName(Event)));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeNotifyObject(const FAnimNotifyEvent& Event)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("notify_name"), Event.NotifyName.ToString());
    Json->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
    Json->SetNumberField(TEXT("end_trigger_time"), Event.GetEndTriggerTime());
    Json->SetNumberField(TEXT("duration"), Event.GetDuration());
    Json->SetNumberField(TEXT("track_index"), Event.TrackIndex);
    Json->SetStringField(TEXT("notify_class"), NotifyClassName(Event));
    Json->SetBoolField(TEXT("is_state"), Event.NotifyStateClass != nullptr);
    return Json;
}

void SetStringColumns(const TSharedPtr<FJsonObject>& Data, const TCHAR* Field, const TArray<FString>& Names)
{
    TArray<TSharedPtr<FJsonValue>> Columns;
    for (const FString& Name : Names)
    {
        Columns.Add(MakeShared<FJsonValueString>(Name));
    }
    Data->SetArrayField(Field, Columns);
}
}

TSharedPtr<FJsonObject> HandleAnimMontageSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *AssetPath);
    if (Montage == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("AnimMontage could not be loaded")));
        return Response;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Sections;
    for (int32 Index = 0; Index < Montage->GetNumSections(); ++Index)
    {
        Sections.Add(bCompact ? MakeSectionRow(Montage, Index) : MakeShared<FJsonValueObject>(MakeSectionObject(Montage, Index)));
    }

    TArray<TSharedPtr<FJsonValue>> Slots;
    TArray<TSharedPtr<FJsonValue>> Segments;
    for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
    {
        const FString SlotName = Slot.SlotName.ToString();
        if (bCompact)
        {
            TArray<TSharedPtr<FJsonValue>> Row;
            Row.Add(MakeShared<FJsonValueString>(SlotName));
            Row.Add(MakeShared<FJsonValueNumber>(Slot.AnimTrack.AnimSegments.Num()));
            Slots.Add(MakeShared<FJsonValueArray>(Row));
        }
        else
        {
            TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
            Json->SetStringField(TEXT("slot_name"), SlotName);
            Json->SetNumberField(TEXT("segment_count"), Slot.AnimTrack.AnimSegments.Num());
            Slots.Add(MakeShared<FJsonValueObject>(Json));
        }

        for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
        {
            Segments.Add(bCompact ? MakeSegmentRow(SlotName, Segment) : MakeSegmentObject(SlotName, Segment));
        }
    }

    TArray<TSharedPtr<FJsonValue>> Notifies;
    for (const FAnimNotifyEvent& Event : Montage->Notifies)
    {
        Notifies.Add(bCompact ? MakeNotifyRow(Event) : MakeShared<FJsonValueObject>(MakeNotifyObject(Event)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Montage->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Montage->GetClass()->GetPathName());
    Data->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("anim_montage_summary_compact"));
        SetStringColumns(Data, TEXT("section_columns"), { TEXT("name"), TEXT("start"), TEXT("length"), TEXT("next") });
        SetStringColumns(Data, TEXT("slot_columns"), { TEXT("slot"), TEXT("segment_count") });
        SetStringColumns(Data, TEXT("segment_columns"), { TEXT("slot"), TEXT("animation"), TEXT("start_pos"), TEXT("anim_start"), TEXT("anim_end"), TEXT("play_rate") });
        SetStringColumns(Data, TEXT("notify_columns"), { TEXT("name"), TEXT("trigger"), TEXT("duration"), TEXT("track"), TEXT("notify_class") });
    }
    Data->SetArrayField(TEXT("sections"), Sections);
    Data->SetArrayField(TEXT("slots"), Slots);
    Data->SetArrayField(TEXT("segments"), Segments);
    Data->SetArrayField(TEXT("notifies"), Notifies);
    Data->SetNumberField(TEXT("section_count"), Sections.Num());
    Data->SetNumberField(TEXT("slot_count"), Slots.Num());
    Data->SetNumberField(TEXT("segment_count"), Segments.Num());
    Data->SetNumberField(TEXT("notify_count"), Notifies.Num());
    Data->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
