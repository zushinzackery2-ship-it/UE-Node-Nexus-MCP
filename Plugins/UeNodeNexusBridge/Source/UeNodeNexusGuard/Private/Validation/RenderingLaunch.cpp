#include "Environment.h"

#include "Misc/CommandLine.h"

namespace NexusLifecycle::Validation
{
TSharedPtr<FJsonObject> RenderingLaunch()
{
    TArray<FString> Tokens, Switches;
    FCommandLine::Parse(FCommandLine::Get(), Tokens, Switches);
    TArray<TSharedPtr<FJsonValue>> Arguments, Unsupported;
    const TSet<FString> Flags
    {
        TEXT("sm5"), TEXT("sm6"), TEXT("featureleveles31"), TEXT("noraytracing"), TEXT("raytracing"),
        TEXT("renderoffscreen"), TEXT("allowsoftwarerendering"), TEXT("windowed"), TEXT("fullscreen"),
        TEXT("forceres"), TEXT("vsync"), TEXT("novsync"), TEXT("norhithread"), TEXT("onethread")
    };
    const TSet<FString> Values
    {
        TEXT("resx"), TEXT("resy"), TEXT("graphicsadapter"), TEXT("adapter"), TEXT("featurelevel")
    };
    for (const FString& Switch : Switches)
    {
        FString Name, Value;
        const bool bValue = Switch.Split(TEXT("="), &Name, &Value);
        Name = (bValue ? Name : Switch).ToLower();
        if (Name == TEXT("d3d11") || Name == TEXT("d3d12") || Name == TEXT("dx11") || Name == TEXT("dx12"))
        {
            continue;
        }
        if ((!bValue && Flags.Contains(Name)) || (bValue && Values.Contains(Name)))
        {
            Arguments.Add(MakeShared<FJsonValueString>(TEXT("-") + Name + (bValue ? TEXT("=") + Value : TEXT(""))));
            continue;
        }
        if (Name == TEXT("execcmds") || Name.StartsWith(TEXT("ini")) || Name.StartsWith(TEXT("r."))
            || Name.Contains(TEXT("render")) || Name.Contains(TEXT("rhi")) || Name.Contains(TEXT("shader"))
            || Name.Contains(TEXT("raytrac")) || Name.Contains(TEXT("vulkan")) || Name.Contains(TEXT("featurelevel"))
            || Name.Contains(TEXT("gpu")) || Name.Contains(TEXT("d3d")) || Name.Contains(TEXT("quality")))
        {
            Unsupported.Add(MakeShared<FJsonValueString>(TEXT("-") + Switch));
        }
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("arguments"), Arguments);
    Result->SetArrayField(TEXT("unsupported"), Unsupported);
    return Result;
}
}
