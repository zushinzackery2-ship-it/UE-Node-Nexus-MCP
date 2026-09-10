#include "UeNodeNexusBridgeCompilation.h"

#include "Engine/Blueprint.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
FBridgeAssetCompileDiagnostics InspectAssetDiagnostics(UObject* Asset, const FString& AssetPath)
{
    if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
    {
        return MaterialResourceStatus(Material, false);
    }
    FBridgeAssetCompileDiagnostics Result;
    if (!Asset)
    {
        return Result;
    }
    Result.AssetClass = Asset->GetClass()->GetPathName();
    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        Result.bSupported = true;
        Result.bOk = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings;
        Result.State = Result.bOk ? TEXT("ready") : TEXT("pending");
        if (Blueprint->Status == BS_Error)
        {
            Result.State = TEXT("failed");
            Result.ErrorCount = 1;
            Result.Diagnostics.Add(MakeShared<FJsonValueObject>(MakeDiagnostic(
                TEXT("error"), TEXT("blueprint_compile_error"), TEXT("Blueprint has compilation errors; asset_compile returns full compiler messages"),
                AssetPath, TEXT("Unreal.BlueprintCompiler"))));
        }
    }
    else if (Cast<UMaterialFunction>(Asset))
    {
        // GetPreviewMaterial creates and compiles a preview, so a passive read cannot call it.
        Result.bSupported = true;
        Result.State = TEXT("unknown");
    }
    return Result;
}
}
