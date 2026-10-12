#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "NexusViewportDiagnosticsLibrary.generated.h"

UCLASS()
class UENODENEXUSBRIDGE_API UNexusViewportDiagnosticsLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString PrimaryViewportStateJson();

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString SetPrimaryViewportInputEnabledJson(bool Enabled, int32 ViewKey = 0);

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString ResetPrimaryViewportHistoryJson();

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString CapturePrimaryAtPhaseJson(const FString& Filename, int32 TemporalSample, int32 RenderFrameMod8, float TimeoutSeconds);

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString CapturePrimaryAtShaderPhaseJson(const FString& Filename, int32 TemporalSample, int32 ShaderFrameMod8, float TimeoutSeconds);

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString QueuePrimaryPixelProbeJson(int32 X, int32 Y, int32 Width, int32 Height,
        int32 TemporalSample, int32 ShaderFrameMod8, float TimeoutSeconds = 30.0f);

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static FString ReadPrimaryPixelProbeJson();

    UFUNCTION(BlueprintCallable, Category="Nexus|Diagnostics")
    static bool ProfilePrimaryGpu();
};
