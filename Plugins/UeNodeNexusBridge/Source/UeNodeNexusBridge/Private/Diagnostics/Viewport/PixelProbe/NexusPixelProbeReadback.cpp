#include "NexusPixelProbeState.h"
#include "../NexusViewportDiagnosticsLog.h"

#include "Misc/Base64.h"
#include "RenderingThread.h"

namespace UeNodeNexusBridge::Diagnostics
{
void CompletePixelProbeReadback_RenderThread(FProbeRequest& Probe)
{
    check(IsInRenderingThread());
    ExpirePixelProbe_RenderThread(Probe);
    if (!Probe.bReading)
    {
        return;
    }
    for (const FProbeTexture& Entry : Probe.Textures)
    {
        if (!Entry.Readback->IsReady())
        {
            return;
        }
    }
    // Keep submitted GPU resources alive through an expired request until the fences signal.
    if (Probe.Result->GetStringField(TEXT("state")) == TEXT("failed"))
    {
        Probe.Textures.Reset();
        Probe.bReading = false;
        return;
    }
    TArray<TSharedPtr<FJsonValue>> Textures;
    int32 TotalBytes = 0;
    for (FProbeTexture& Entry : Probe.Textures)
    {
        int32 Pitch = 0;
        const uint8* Data = static_cast<const uint8*>(Entry.Readback->Lock(Pitch));
        const int32 BytesPerPixel = GPixelFormats[Entry.Format].BlockBytes;
        const int32 RowBytes = Entry.Rect.Width() * BytesPerPixel;
        check(Data && Pitch >= Entry.Rect.Width());
        TArray<uint8> Packed;
        Packed.SetNumUninitialized(RowBytes * Entry.Rect.Height());
        for (int32 Row = 0; Row < Entry.Rect.Height(); ++Row)
        {
            FMemory::Memcpy(Packed.GetData() + Row * RowBytes, Data + Row * Pitch * BytesPerPixel, RowBytes);
        }
        Entry.Readback->Unlock();
        TotalBytes += Packed.Num();
        auto Texture = MakeShared<FJsonObject>();
        Texture->SetStringField(TEXT("name"), Entry.Name);
        Texture->SetStringField(TEXT("format"), GPixelFormats[Entry.Format].Name);
        Texture->SetNumberField(TEXT("bytes_per_pixel"), BytesPerPixel);
        Texture->SetNumberField(TEXT("x"), Entry.Rect.Min.X);
        Texture->SetNumberField(TEXT("y"), Entry.Rect.Min.Y);
        Texture->SetNumberField(TEXT("width"), Entry.Rect.Width());
        Texture->SetNumberField(TEXT("height"), Entry.Rect.Height());
        Texture->SetStringField(TEXT("bytes_base64"), FBase64::Encode(Packed));
        Textures.Add(MakeShared<FJsonValueObject>(Texture));
    }
    Probe.Result->SetArrayField(TEXT("textures"), Textures);
    Probe.Result->SetStringField(TEXT("state"), TEXT("completed"));
    UE_LOG(LogNexusViewportDiagnostics, Display, TEXT("pixel_probe view=%u state=completed textures=%d bytes=%d"),
        Probe.ViewKey, Textures.Num(), TotalBytes);
    Probe.Textures.Reset();
    Probe.bReading = false;
}
}
