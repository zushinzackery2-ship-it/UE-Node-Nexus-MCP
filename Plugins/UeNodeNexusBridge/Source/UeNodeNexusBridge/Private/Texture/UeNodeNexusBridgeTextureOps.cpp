#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "PixelFormat.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
template <typename TEnum>
FString EnumValueToString(TEnum Value)
{
    if (const UEnum* EnumType = StaticEnum<TEnum>())
    {
        return EnumType->GetNameStringByValue(static_cast<int64>(Value));
    }
    return FString::FromInt(static_cast<int64>(Value));
}
}

TSharedPtr<FJsonObject> HandleTextureSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> ErrorResponse;
    UTexture2D* Texture = LoadAssetOrError<UTexture2D>(Payload, Operation, RequestId, ErrorResponse, TEXT("Texture2D"));
    if (Texture == nullptr)
    {
        return ErrorResponse;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Texture->GetPathName());
    Data->SetStringField(TEXT("asset_class"), Texture->GetClass()->GetPathName());
    Data->SetNumberField(TEXT("width"), Texture->GetSizeX());
    Data->SetNumberField(TEXT("height"), Texture->GetSizeY());

    const FIntPoint ImportedSize = Texture->GetImportedSize();
    Data->SetNumberField(TEXT("imported_width"), ImportedSize.X);
    Data->SetNumberField(TEXT("imported_height"), ImportedSize.Y);

    Data->SetStringField(TEXT("compression_format"), GetPixelFormatString(Texture->GetPixelFormat()));
    Data->SetStringField(TEXT("compression_settings"), EnumValueToString<TextureCompressionSettings>(Texture->CompressionSettings));
    Data->SetBoolField(TEXT("srgb"), Texture->SRGB != 0);
    Data->SetStringField(TEXT("lod_group"), EnumValueToString<TextureGroup>(Texture->LODGroup));

#if WITH_EDITORONLY_DATA
    Data->SetNumberField(TEXT("source_width"), static_cast<int32>(Texture->Source.GetSizeX()));
    Data->SetNumberField(TEXT("source_height"), static_cast<int32>(Texture->Source.GetSizeY()));
    Data->SetStringField(TEXT("source_format"), EnumValueToString<ETextureSourceFormat>(Texture->Source.GetFormat()));
#endif

    AddElapsedMs(Data, StartSeconds);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
