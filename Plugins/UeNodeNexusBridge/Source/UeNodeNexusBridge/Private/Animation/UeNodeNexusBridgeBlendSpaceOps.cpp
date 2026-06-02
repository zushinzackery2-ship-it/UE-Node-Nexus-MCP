#include "UeNodeNexusBridgeOperations.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Dom/JsonValue.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
TSharedPtr<FJsonValue> MakeAxisRow(const FBlendParameter& Parameter)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Parameter.DisplayName));
    Row.Add(MakeShared<FJsonValueNumber>(Parameter.Min));
    Row.Add(MakeShared<FJsonValueNumber>(Parameter.Max));
    Row.Add(MakeShared<FJsonValueNumber>(Parameter.GridNum));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeAxisObject(const FBlendParameter& Parameter)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Parameter.DisplayName);
    Json->SetNumberField(TEXT("min"), Parameter.Min);
    Json->SetNumberField(TEXT("max"), Parameter.Max);
    Json->SetNumberField(TEXT("grid_num"), Parameter.GridNum);
    return Json;
}

TSharedPtr<FJsonValue> MakeSampleRow(const FBlendSample& Sample)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Sample.Animation ? Sample.Animation->GetPathName() : FString()));
    Row.Add(MakeShared<FJsonValueNumber>(Sample.SampleValue.X));
    Row.Add(MakeShared<FJsonValueNumber>(Sample.SampleValue.Y));
    Row.Add(MakeShared<FJsonValueNumber>(Sample.RateScale));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> MakeSampleObject(const FBlendSample& Sample)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("animation"), Sample.Animation ? Sample.Animation->GetPathName() : FString());
    Json->SetNumberField(TEXT("x"), Sample.SampleValue.X);
    Json->SetNumberField(TEXT("y"), Sample.SampleValue.Y);
    Json->SetNumberField(TEXT("z"), Sample.SampleValue.Z);
    Json->SetNumberField(TEXT("rate_scale"), Sample.RateScale);
    return Json;
}
}

TSharedPtr<FJsonObject> HandleBlendSpaceSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return Response;
    }

    UBlendSpace* BlendSpace = LoadObject<UBlendSpace>(nullptr, *AssetPath);
    if (BlendSpace == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("BlendSpace could not be loaded")));
        return Response;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    const int32 AxisCount = BlendSpace->IsA<UBlendSpace1D>() ? 1 : 2;
    TArray<TSharedPtr<FJsonValue>> Axes;
    for (int32 Index = 0; Index < AxisCount; ++Index)
    {
        const FBlendParameter& Parameter = BlendSpace->GetBlendParameter(Index);
        Axes.Add(bCompact ? MakeAxisRow(Parameter) : MakeShared<FJsonValueObject>(MakeAxisObject(Parameter)));
    }

    TArray<TSharedPtr<FJsonValue>> Samples;
    for (const FBlendSample& Sample : BlendSpace->GetBlendSamples())
    {
        Samples.Add(bCompact ? MakeSampleRow(Sample) : MakeShared<FJsonValueObject>(MakeSampleObject(Sample)));
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), BlendSpace->GetPathName());
    Data->SetStringField(TEXT("asset_class"), BlendSpace->GetClass()->GetPathName());
    Data->SetNumberField(TEXT("axis_count"), AxisCount);
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("blend_space_summary_compact"));
        TArray<TSharedPtr<FJsonValue>> AxisColumns = { MakeShared<FJsonValueString>(TEXT("name")), MakeShared<FJsonValueString>(TEXT("min")), MakeShared<FJsonValueString>(TEXT("max")), MakeShared<FJsonValueString>(TEXT("grid_num")) };
        TArray<TSharedPtr<FJsonValue>> SampleColumns = { MakeShared<FJsonValueString>(TEXT("animation")), MakeShared<FJsonValueString>(TEXT("x")), MakeShared<FJsonValueString>(TEXT("y")), MakeShared<FJsonValueString>(TEXT("rate_scale")) };
        Data->SetArrayField(TEXT("axis_columns"), AxisColumns);
        Data->SetArrayField(TEXT("sample_columns"), SampleColumns);
    }
    Data->SetArrayField(TEXT("axes"), Axes);
    Data->SetArrayField(TEXT("samples"), Samples);
    Data->SetNumberField(TEXT("sample_count"), Samples.Num());
    Data->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
