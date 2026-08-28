#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString NodeId(USoundNode* Node)
{
    return Node ? Node->GetName() : FString();
}

void AddFloatArray(TArray<FString>& Parts, const FString& Name, const TArray<float>& Values)
{
    if (Values.Num() == 0)
    {
        return;
    }
    TArray<FString> Text;
    for (float Value : Values)
    {
        Text.Add(FString::SanitizeFloat(Value));
    }
    Parts.Add(FString::Printf(TEXT("%s=[%s]"), *Name, *FString::Join(Text, TEXT(","))));
}

FString BuildNodeSummary(USoundNode* Node)
{
    TArray<FString> Parts;
    if (USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(Node))
    {
        Parts.Add(FString::Printf(TEXT("wave=%s"), *ObjectPathOrEmpty(WavePlayer->GetSoundWave())));
        Parts.Add(FString::Printf(TEXT("looping=%s"), WavePlayer->bLooping ? TEXT("true") : TEXT("false")));
    }
    if (USoundNodeModulator* Modulator = Cast<USoundNodeModulator>(Node))
    {
        Parts.Add(FString::Printf(TEXT("pitch=%s..%s"), *FString::SanitizeFloat(Modulator->PitchMin), *FString::SanitizeFloat(Modulator->PitchMax)));
        Parts.Add(FString::Printf(TEXT("volume=%s..%s"), *FString::SanitizeFloat(Modulator->VolumeMin), *FString::SanitizeFloat(Modulator->VolumeMax)));
    }
    if (USoundNodeMixer* Mixer = Cast<USoundNodeMixer>(Node))
    {
        AddFloatArray(Parts, TEXT("input_volume"), Mixer->InputVolume);
    }
    if (USoundNodeConcatenator* Concatenator = Cast<USoundNodeConcatenator>(Node))
    {
        AddFloatArray(Parts, TEXT("input_volume"), Concatenator->InputVolume);
    }
    if (USoundNodeRandom* Random = Cast<USoundNodeRandom>(Node))
    {
        AddFloatArray(Parts, TEXT("weights"), Random->Weights);
        Parts.Add(FString::Printf(TEXT("without_replacement=%s"), Random->bRandomizeWithoutReplacement ? TEXT("true") : TEXT("false")));
    }
    if (USoundNodeDelay* Delay = Cast<USoundNodeDelay>(Node))
    {
        Parts.Add(FString::Printf(TEXT("delay=%s..%s"), *FString::SanitizeFloat(Delay->DelayMin), *FString::SanitizeFloat(Delay->DelayMax)));
    }
    if (USoundNodeAttenuation* Attenuation = Cast<USoundNodeAttenuation>(Node))
    {
        Parts.Add(FString::Printf(TEXT("override_attenuation=%s"), Attenuation->bOverrideAttenuation ? TEXT("true") : TEXT("false")));
        Parts.Add(FString::Printf(TEXT("attenuation=%s"), *ObjectPathOrEmpty(Attenuation->AttenuationSettings)));
    }
    return FString::Join(Parts, TEXT(";"));
}

TSharedPtr<FJsonValue> MakeNodeRow(USoundNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(NodeId(Node)));
    Row.Add(MakeShared<FJsonValueString>(Node ? Node->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(ObjectPathOrEmpty(Node)));
    Row.Add(MakeShared<FJsonValueNumber>(Node ? Node->ChildNodes.Num() : 0));
    Row.Add(MakeShared<FJsonValueString>(BuildNodeSummary(Node)));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeNodeObject(USoundNode* Node)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("node_id"), NodeId(Node));
    Json->SetStringField(TEXT("class"), Node ? Node->GetClass()->GetPathName() : FString());
    Json->SetStringField(TEXT("object_path"), ObjectPathOrEmpty(Node));
    Json->SetNumberField(TEXT("child_count"), Node ? Node->ChildNodes.Num() : 0);
    Json->SetStringField(TEXT("summary"), BuildNodeSummary(Node));
    return Json;
}

TSharedPtr<FJsonValue> MakeEdgeRow(USoundNode* Parent, int32 ChildIndex, USoundNode* Child)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(NodeId(Parent)));
    Row.Add(MakeShared<FJsonValueNumber>(ChildIndex));
    Row.Add(MakeShared<FJsonValueString>(NodeId(Child)));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeEdgeObject(USoundNode* Parent, int32 ChildIndex, USoundNode* Child)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("parent_id"), NodeId(Parent));
    Json->SetNumberField(TEXT("child_index"), ChildIndex);
    Json->SetStringField(TEXT("child_id"), NodeId(Child));
    return Json;
}

void TraverseNode(USoundNode* Node, bool bCompact, TSet<USoundNode*>& Seen, TArray<TSharedPtr<FJsonValue>>& Nodes, TArray<TSharedPtr<FJsonValue>>& Edges)
{
    if (Node == nullptr || Seen.Contains(Node))
    {
        return;
    }

    Seen.Add(Node);
    Nodes.Add(bCompact ? MakeNodeRow(Node) : MakeShared<FJsonValueObject>(MakeNodeObject(Node)));
    for (int32 Index = 0; Index < Node->ChildNodes.Num(); ++Index)
    {
        USoundNode* Child = Node->ChildNodes[Index];
        if (Child == nullptr)
        {
            continue;
        }
        Edges.Add(bCompact ? MakeEdgeRow(Node, Index, Child) : MakeShared<FJsonValueObject>(MakeEdgeObject(Node, Index, Child)));
        TraverseNode(Child, bCompact, Seen, Nodes, Edges);
    }
}
}

TSharedPtr<FJsonObject> HandleSoundCueSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> ErrorResponse;
    USoundCue* SoundCue = LoadAssetOrError<USoundCue>(Payload, Operation, RequestId, ErrorResponse, TEXT("SoundCue"));
    if (SoundCue == nullptr)
    {
        return ErrorResponse;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Edges;
    TSet<USoundNode*> Seen;
    TraverseNode(SoundCue->FirstNode, bCompact, Seen, Nodes, Edges);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), SoundCue->GetPathName());
    Data->SetStringField(TEXT("asset_class"), SoundCue->GetClass()->GetPathName());
    Data->SetStringField(TEXT("first_node"), NodeId(SoundCue->FirstNode));
    Data->SetNumberField(TEXT("volume_multiplier"), SoundCue->VolumeMultiplier);
    Data->SetNumberField(TEXT("pitch_multiplier"), SoundCue->PitchMultiplier);
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("sound_cue_summary_compact"));
        Data->SetArrayField(TEXT("node_columns"), { MakeShared<FJsonValueString>(TEXT("node_id")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("object_path")), MakeShared<FJsonValueString>(TEXT("child_count")), MakeShared<FJsonValueString>(TEXT("summary")) });
        Data->SetArrayField(TEXT("edge_columns"), { MakeShared<FJsonValueString>(TEXT("parent_id")), MakeShared<FJsonValueString>(TEXT("child_index")), MakeShared<FJsonValueString>(TEXT("child_id")) });
    }
    Data->SetArrayField(TEXT("nodes"), Nodes);
    Data->SetArrayField(TEXT("edges"), Edges);
    Data->SetNumberField(TEXT("node_count"), Nodes.Num());
    Data->SetNumberField(TEXT("edge_count"), Edges.Num());
    AddElapsedMs(Data, StartSeconds);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
