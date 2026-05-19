#include "UeNodeNexusBridgeOperations.h"

#include "Animation/AnimBlueprint.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "UeNodeNexusBridgeJson.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static bool IsTrackedAnimNodeClass(const FString& ClassName)
{
    static const TSet<FString> ExactNames = {
        TEXT("AnimGraphNode_ControlRig"),
        TEXT("AnimGraphNode_BlendSpacePlayer"),
        TEXT("AnimGraphNode_BlendSpaceEvaluator"),
        TEXT("AnimGraphNode_ApplyMeshSpaceAdditive"),
        TEXT("AnimGraphNode_LayeredBoneBlend"),
        TEXT("AnimGraphNode_TwoBoneIK"),
        TEXT("AnimGraphNode_Slot"),
        TEXT("AnimGraphNode_SaveCachedPose"),
        TEXT("AnimGraphNode_UseCachedPose"),
        TEXT("AnimGraphNode_SequencePlayer"),
        TEXT("AnimGraphNode_SequenceEvaluator"),
        TEXT("AnimGraphNode_RotationOffsetBlendSpace"),
        TEXT("AnimGraphNode_LinkedAnimGraph"),
        TEXT("AnimGraphNode_LinkedAnimLayer"),
        TEXT("AnimGraphNode_CopyPoseFromMesh")
    };

    return ExactNames.Contains(ClassName) || ClassName.Contains(TEXT("ControlRig")) || ClassName.Contains(TEXT("IK")) || ClassName.Contains(TEXT("Blend"));
}

static FString CleanSummaryText(FString Value)
{
    Value.RemoveFromStart(TEXT("("));
    Value.RemoveFromEnd(TEXT(")"));
    Value.ReplaceInline(TEXT("\r"), TEXT(" "));
    Value.ReplaceInline(TEXT("\n"), TEXT(" "));
    Value.ReplaceInline(TEXT("\""), TEXT(""));
    return Value.Left(160);
}

static bool ExportFieldValue(UObject* Owner, const void* Container, FProperty* Property, FString& OutValue)
{
    if (Property == nullptr || Container == nullptr)
    {
        return false;
    }

    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
        UObject* ValueObject = ObjectProperty->GetObjectPropertyValue(ValuePtr);
        OutValue = ValueObject ? ValueObject->GetPathName() : FString();
        return !OutValue.IsEmpty();
    }

    FString Value;
    const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
    Property->ExportTextItem_Direct(Value, ValuePtr, nullptr, Owner, PPF_None);
    OutValue = CleanSummaryText(Value);
    return !OutValue.IsEmpty() && !OutValue.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}

static FProperty* FindPropertyCaseInsensitive(UStruct* Struct, const FString& Name)
{
    if (Struct == nullptr)
    {
        return nullptr;
    }

    for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property != nullptr && Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return Property;
        }
    }
    return nullptr;
}

static void AddField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& FieldName, const FString& Alias)
{
    FString Value;
    if (ExportFieldValue(Owner, Container, FindPropertyCaseInsensitive(Struct, FieldName), Value))
    {
        Parts.Add(FString::Printf(TEXT("%s=%s"), *Alias, *Value));
    }
}

static void AddStructField(TArray<FString>& Parts, UObject* Owner, const void* Container, UStruct* Struct, const FString& StructFieldName, const FString& InnerFieldName, const FString& Alias)
{
    FStructProperty* StructProperty = CastField<FStructProperty>(FindPropertyCaseInsensitive(Struct, StructFieldName));
    if (StructProperty == nullptr)
    {
        return;
    }

    const void* StructPtr = StructProperty->ContainerPtrToValuePtr<void>(Container);
    AddField(Parts, Owner, StructPtr, StructProperty->Struct, InnerFieldName, Alias);
}

static FStructProperty* FindAnimNodeStructProperty(UEdGraphNode* Node)
{
    if (Node == nullptr)
    {
        return nullptr;
    }

    if (FStructProperty* NodeProperty = CastField<FStructProperty>(Node->GetClass()->FindPropertyByName(TEXT("Node"))))
    {
        return NodeProperty;
    }

    for (TFieldIterator<FStructProperty> It(Node->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FStructProperty* Property = *It;
        if (Property != nullptr && Property->Struct != nullptr && Property->Struct->GetName().StartsWith(TEXT("AnimNode_")))
        {
            return Property;
        }
    }
    return nullptr;
}

static FString BuildAnimNodeSummary(UEdGraphNode* Node)
{
    TArray<FString> Parts;
    FStructProperty* AnimNodeProperty = FindAnimNodeStructProperty(Node);
    if (AnimNodeProperty != nullptr)
    {
        const void* AnimNodePtr = AnimNodeProperty->ContainerPtrToValuePtr<void>(Node);
        UStruct* AnimNodeStruct = AnimNodeProperty->Struct;
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("Alpha"), TEXT("alpha"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("AlphaInputType"), TEXT("alpha_type"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("bApplyAdditive"), TEXT("additive"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("bMeshSpaceRotationBlend"), TEXT("mesh_rot"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("BlendSpace"), TEXT("blend_space"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("Sequence"), TEXT("sequence"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("SlotName"), TEXT("slot"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("ControlRigClass"), TEXT("rig"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("X"), TEXT("x"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("Y"), TEXT("y"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("BlendMode"), TEXT("blend_mode"));
        AddField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("LayerSetup"), TEXT("layers"));
        AddStructField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("IKBone"), TEXT("BoneName"), TEXT("ik_bone"));
        AddStructField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("EffectorTarget"), TEXT("Bone"), TEXT("eff_bone"));
        AddStructField(Parts, Node, AnimNodePtr, AnimNodeStruct, TEXT("EffectorTarget"), TEXT("Socket"), TEXT("eff_socket"));
    }

    AddField(Parts, Node, Node, Node->GetClass(), TEXT("CacheName"), TEXT("cache"));
    AddField(Parts, Node, Node, Node->GetClass(), TEXT("NameOfCache"), TEXT("cache"));
    AddField(Parts, Node, Node, Node->GetClass(), TEXT("NodeTag"), TEXT("tag"));
    return FString::Join(Parts, TEXT(";"));
}

static TSharedPtr<FJsonValue> MakeAnimNodeRow(UEdGraph* Graph, UEdGraphNode* Node)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)));
    Row.Add(MakeShared<FJsonValueString>(Graph ? Graph->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueString>(Node->GetClass()->GetName()));
    Row.Add(MakeShared<FJsonValueString>(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
    Row.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
    Row.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
    Row.Add(MakeShared<FJsonValueString>(BuildAnimNodeSummary(Node)));
    return MakeShared<FJsonValueArray>(Row);
}

static TSharedPtr<FJsonObject> MakeAnimNodeObject(UEdGraph* Graph, UEdGraphNode* Node)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
    Json->SetStringField(TEXT("graph"), Graph ? Graph->GetName() : FString());
    Json->SetStringField(TEXT("class"), Node->GetClass()->GetName());
    Json->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
    Json->SetNumberField(TEXT("x"), Node->NodePosX);
    Json->SetNumberField(TEXT("y"), Node->NodePosY);
    Json->SetStringField(TEXT("summary"), BuildAnimNodeSummary(Node));
    return Json;
}

TSharedPtr<FJsonObject> HandleAnimBlueprintSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
    if (AnimBlueprint == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("AnimBlueprint could not be loaded")));
        return Response;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    double NumericMaxNodes = 200.0;
    Payload->TryGetNumberField(TEXT("max_nodes"), NumericMaxNodes);
    const int32 MaxNodes = FMath::Clamp(static_cast<int32>(NumericMaxNodes), 1, 1000);

    TArray<UEdGraph*> Graphs;
    AnimBlueprint->GetAllGraphs(Graphs);

    TArray<TSharedPtr<FJsonValue>> Items;
    TMap<FString, int32> TypeCounts;
    int32 TotalMatched = 0;
    bool bTruncated = false;

    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr || !IsTrackedAnimNodeClass(Node->GetClass()->GetName()))
            {
                continue;
            }

            ++TotalMatched;
            TypeCounts.FindOrAdd(Node->GetClass()->GetName())++;
            if (Items.Num() >= MaxNodes)
            {
                bTruncated = true;
                continue;
            }

            Items.Add(bCompact ? MakeAnimNodeRow(Graph, Node) : MakeShared<FJsonValueObject>(MakeAnimNodeObject(Graph, Node)));
        }
    }

    TArray<TSharedPtr<FJsonValue>> TypeRows;
    for (const TPair<FString, int32>& Pair : TypeCounts)
    {
        TArray<TSharedPtr<FJsonValue>> Row;
        Row.Add(MakeShared<FJsonValueString>(Pair.Key));
        Row.Add(MakeShared<FJsonValueNumber>(Pair.Value));
        TypeRows.Add(MakeShared<FJsonValueArray>(Row));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AnimBlueprint->GetPathName());
    Data->SetStringField(TEXT("asset_class"), AnimBlueprint->GetClass()->GetPathName());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("anim_blueprint_summary_compact"));
        Data->SetArrayField(TEXT("columns"), { MakeShared<FJsonValueString>(TEXT("node_id")), MakeShared<FJsonValueString>(TEXT("graph")), MakeShared<FJsonValueString>(TEXT("class")), MakeShared<FJsonValueString>(TEXT("title")), MakeShared<FJsonValueString>(TEXT("x")), MakeShared<FJsonValueString>(TEXT("y")), MakeShared<FJsonValueString>(TEXT("summary")) });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetArrayField(TEXT("type_counts"), TypeRows);
    Data->SetNumberField(TEXT("returned_count"), Items.Num());
    Data->SetNumberField(TEXT("total_count"), TotalMatched);
    Data->SetBoolField(TEXT("truncated"), bTruncated);
    Data->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
