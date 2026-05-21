#include "UeNodeNexusBridgeMaterialPatchApplyOps.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "UeNodeNexusBridgeMaterialPatchConnectOps.h"
#include "UeNodeNexusBridgeMaterialPatchContext.h"
#include "UeNodeNexusBridgeMaterialPatchNodeOps.h"
#include "UeNodeNexusBridgeMaterialPatchShared.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialPatchOperation(
    UMaterial* Material,
    const TSharedPtr<FJsonObject>& Op,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    TArray<TSharedPtr<FJsonValue>>& Diagnostics,
    FMaterialPatchContext& Context)
{
    FString OpName;
    if (!Op->TryGetStringField(TEXT("op"), OpName))
    {
        AddMaterialPatchDiagnostic(Diagnostics, TEXT("missing_patch_op"), TEXT("Material patch operation is missing op"), Material);
        return false;
    }
    if (OpName == TEXT("connect_pins"))
    {
        return ApplyMaterialPatchConnect(Material, Op, bDryRun, Diff, Diagnostics, Context);
    }
    if (OpName == TEXT("disconnect_pins"))
    {
        return ApplyMaterialPatchDisconnect(Material, Op, bDryRun, Diff, Context);
    }
    if (OpName == TEXT("create_node"))
    {
        return ApplyMaterialPatchCreateNode(Material, Op, bDryRun, Diff, Diagnostics, Context);
    }

    return ApplyMaterialPatchNodeOperation(Material, OpName, Op, bDryRun, Diff, Diagnostics, Context);
}
}
