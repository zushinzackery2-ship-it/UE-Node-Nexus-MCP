#include "NexusCapture.h"

#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge::Capture
{
bool WriteReceipt(const TSharedPtr<FJsonObject>& Receipt)
{
    const FString Path = Receipt->GetStringField(TEXT("receipt_path"));
    const FString Temporary = Path + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(SerializeJsonObjectToString(Receipt), *Temporary,
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        return false;
    }
    if (!IFileManager::Get().Move(*Path, *Temporary, true, true))
    {
        IFileManager::Get().Delete(*Temporary);
        return false;
    }
    return true;
}

bool VerifyImage(const FString& Path, int32& Width, int32& Height, FString* Digest)
{
    TArray64<uint8> Bytes;
    const int64 FileSize = IFileManager::Get().FileSize(*Path);
    if (FileSize < 33 || FileSize > 268435456 || !FFileHelper::LoadFileToArray(Bytes, *Path))
    {
        return false;
    }
    auto& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    const TSharedPtr<IImageWrapper> Image = Module.CreateImageWrapper(EImageFormat::PNG);
    if (!Image.IsValid() || !Image->SetCompressed(Bytes.GetData(), Bytes.Num()))
    {
        return false;
    }
    Width = Image->GetWidth();
    Height = Image->GetHeight();
    TArray64<uint8> Pixels;
    if (Digest)
    {
        FSHAHash Hash;
        FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
        *Digest = Hash.ToString().ToLower();
    }
    return Width > 0 && Height > 0 && int64(Width) * Height <= 67108864
        && Image->GetRaw(ERGBFormat::BGRA, 8, Pixels)
        && Pixels.Num() == int64(Width) * Height * 4;
}
}
