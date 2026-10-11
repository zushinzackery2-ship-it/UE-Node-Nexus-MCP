#include "Tests/Material/Preview/NexusPreviewRender.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UObject/StrongObjectPtr.h"

void VerifyPreviewPixels(FAutomationTestBase& Test, UMaterial* Material, float Expected)
{
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage()));
    Target->InitCustomFormat(4, 4, PF_FloatRGBA, true);
    Target->UpdateResourceImmediate(true);
    FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
    FCanvas Canvas(Resource, nullptr, static_cast<UWorld*>(nullptr), GMaxRHIFeatureLevel);
    FCanvasTileItem Tile(FVector2D::ZeroVector, Material->GetRenderProxy(), FVector2D(4, 4));
    Tile.BlendMode = SE_BLEND_Opaque;
    Canvas.DrawItem(Tile);
    Canvas.Flush_GameThread();
    FlushRenderingCommands();
    TArray<FLinearColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_MinMax);
    Flags.SetLinearToGamma(false);
    Test.TestTrue(TEXT("Read actual preview GPU pixels"), Resource->ReadLinearColorPixels(Pixels, Flags));
    Test.TestEqual(TEXT("GPU readback contains every pixel"), Pixels.Num(), 16);
    for (const FLinearColor& Pixel : Pixels)
    {
        Test.TestTrue(TEXT("Preview GPU pixels use the current function value"),
            FMath::IsNearlyEqual(Pixel.R, Expected, 0.001f)
            && FMath::IsNearlyEqual(Pixel.G, Expected, 0.001f)
            && FMath::IsNearlyEqual(Pixel.B, Expected, 0.001f));
    }
    UE_LOG(LogTemp, Display, TEXT("Nexus preview GPU expected=%.3f pixels=%d"), Expected, Pixels.Num());
}
