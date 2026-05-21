#include "UeNodeNexusBridgeMaterialCustomParamApply.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "UeNodeNexusBridgeMaterialCustomParamOps.h"
#include "UeNodeNexusBridgeMaterialPatchHelpers.h"

namespace UeNodeNexusBridge
{
bool ApplyMaterialCustomJsonParam(
    UMaterial* Material,
    UMaterialExpression* Expression,
    const FString& Name,
    const TSharedPtr<FJsonValue>& Value,
    bool bDryRun,
    TSharedPtr<FJsonObject> Diff,
    FString& OutFailureReason)
{
    UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
    if (Custom == nullptr)
    {
        return false;
    }

    FString OldValue;
    bool bApplied = false;
    if (Name.Equals(TEXT("Inputs"), ESearchCase::IgnoreCase))
    {
        bApplied = ApplyMaterialCustomInputsJson(Custom, Value, bDryRun, OldValue, OutFailureReason);
    }
    else if (Name.Equals(TEXT("AdditionalOutputs"), ESearchCase::IgnoreCase))
    {
        bApplied = ApplyMaterialCustomAdditionalOutputsJson(Custom, Value, bDryRun, OldValue, OutFailureReason);
    }
    else if (Name.Equals(TEXT("AdditionalDefines"), ESearchCase::IgnoreCase))
    {
        bApplied = ApplyMaterialCustomAdditionalDefinesJson(Custom, Value, bDryRun, OldValue, OutFailureReason);
    }
    else if (Name.Equals(TEXT("IncludeFilePaths"), ESearchCase::IgnoreCase))
    {
        bApplied = ApplyMaterialCustomIncludeFilePathsJson(Custom, Value, bDryRun, OldValue, OutFailureReason);
    }

    if (bApplied)
    {
        AddMaterialParamChange(Diff, MaterialExpressionNodeId(Expression), Name, OldValue, MaterialCustomJsonValueToCompactText(Value));
        return true;
    }
    return false;
}
}
