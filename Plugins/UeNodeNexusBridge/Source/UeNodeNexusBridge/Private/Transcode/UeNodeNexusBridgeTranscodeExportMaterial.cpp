#include "UeNodeNexusBridgeTranscode.h"

#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Patch/UeNodeNexusBridgeMaterialPatchHelpers.h"
#include "VT/RuntimeVirtualTexture.h"

namespace UeNodeNexusBridge::Transcode
{
static TSharedPtr<FJsonObject> ExpressionNodeJson(UMaterialExpression* Expression, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions)
{
    TSharedPtr<FJsonObject> Node = MakeShared<FJsonObject>();
    Node->SetStringField(TEXT("guid"), MaterialExpressionKey(Expression, Expressions));
    Node->SetStringField(TEXT("class"), Expression->GetClass()->GetPathName());
    FString Short = Expression->GetClass()->GetName();
    Short.RemoveFromStart(TEXT("MaterialExpression"));
    Node->SetStringField(TEXT("class_short"), Short);
    Node->SetStringField(TEXT("name"), Expression->GetName());
    Node->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
    Node->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);

    TArray<TSharedPtr<FJsonValue>> Props = ExportEditableProps(Expression);
    if (UMaterialExpressionNamedRerouteUsage* Usage = Cast<UMaterialExpressionNamedRerouteUsage>(Expression))
    {
        TSharedPtr<FJsonObject> Synthetic = MakeShared<FJsonObject>();
        Synthetic->SetStringField(TEXT("name"), TEXT("DeclarationName"));
        Synthetic->SetStringField(TEXT("type"), TEXT("FName"));
        Synthetic->SetStringField(TEXT("value"), Usage->Declaration ? Usage->Declaration->Name.ToString() : FString());
        Synthetic->SetStringField(TEXT("default"), FString());
        Props.Add(MakeShared<FJsonValueObject>(Synthetic));
    }
    Node->SetArrayField(TEXT("props"), Props);

    TArray<TSharedPtr<FJsonValue>> Inputs;
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        // Function-call inputs display as "Name (Type)"; the text uses the bare name.
        FString InputName = Expression->GetInputName(It.Index).ToString();
        const int32 SuffixIndex = InputName.Find(TEXT(" ("), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
        if (SuffixIndex > 0 && InputName.EndsWith(TEXT(")")))
        {
            InputName.LeftInline(SuffixIndex);
        }
        Inputs.Add(MakeShared<FJsonValueString>(InputName));
    }
    TArray<TSharedPtr<FJsonValue>> Outputs;
    for (const FExpressionOutput& Output : Expression->GetOutputs())
    {
        Outputs.Add(MakeShared<FJsonValueString>(Output.OutputName.IsNone() ? FString() : Output.OutputName.ToString()));
    }
    Node->SetArrayField(TEXT("inputs"), Inputs);
    Node->SetArrayField(TEXT("outputs"), Outputs);
    return Node;
}

static void AppendExpressionLinks(UMaterialExpression* Expression, TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, TArray<TSharedPtr<FJsonValue>>& Links)
{
    for (FExpressionInputIterator It{ Expression }; It; ++It)
    {
        FExpressionInput* Input = It.Input;
        if (Input == nullptr || Input->Expression == nullptr)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("from"), MaterialExpressionKey(Input->Expression, Expressions));
        Link->SetNumberField(TEXT("from_out"), Input->OutputIndex);
        Link->SetStringField(TEXT("to"), MaterialExpressionKey(Expression, Expressions));
        Link->SetNumberField(TEXT("to_in"), It.Index);
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
}

static TSharedPtr<FJsonObject> GraphJson(TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, UMaterial* MaterialForOutputs)
{
    TArray<TSharedPtr<FJsonValue>> Nodes;
    TArray<TSharedPtr<FJsonValue>> Links;
    for (const TObjectPtr<UMaterialExpression>& ExpressionPtr : Expressions)
    {
        if (UMaterialExpression* Expression = ExpressionPtr.Get())
        {
            Nodes.Add(MakeShared<FJsonValueObject>(ExpressionNodeJson(Expression, Expressions)));
            AppendExpressionLinks(Expression, Expressions, Links);
        }
    }
    TArray<TSharedPtr<FJsonValue>> Outputs;
    if (MaterialForOutputs != nullptr)
    {
        for (EMaterialProperty Property : MaterialOutputProperties())
        {
            FExpressionInput* Input = MaterialForOutputs->GetExpressionInputForProperty(Property);
            if (Input == nullptr || Input->Expression == nullptr)
            {
                continue;
            }
            TSharedPtr<FJsonObject> Output = MakeShared<FJsonObject>();
            Output->SetStringField(TEXT("from"), MaterialExpressionKey(Input->Expression, Expressions));
            Output->SetNumberField(TEXT("from_out"), Input->OutputIndex);
            Output->SetStringField(TEXT("property"), MaterialOutputPropertyName(Property));
            Outputs.Add(MakeShared<FJsonValueObject>(Output));
        }
    }
    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetArrayField(TEXT("nodes"), Nodes);
    Graph->SetArrayField(TEXT("links"), Links);
    Graph->SetArrayField(TEXT("outputs"), Outputs);
    return Graph;
}

TSharedPtr<FJsonObject> BuildMaterialRaw(UMaterial* Material)
{
    if (Material == nullptr)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Material, TEXT("material"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Material));
    Raw->SetObjectField(TEXT("graph"), GraphJson(Material->GetExpressions(), Material));
    return Raw;
}

TSharedPtr<FJsonObject> BuildMaterialFunctionRaw(UMaterialFunction* Function)
{
    if (Function == nullptr)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Function, TEXT("material_function"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Function));
    Raw->SetObjectField(TEXT("graph"), GraphJson(Function->GetExpressions(), nullptr));
    return Raw;
}

template <typename TParameterValue>
static TArray<TSharedPtr<FJsonValue>> ParameterRows(const TArray<TParameterValue>& Values, const TCHAR* TypeName, TFunctionRef<FString(const TParameterValue&)> ValueText)
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const TParameterValue& Value : Values)
    {
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Value.ParameterInfo.Name.ToString());
        Row->SetStringField(TEXT("type"), TypeName);
        Row->SetStringField(TEXT("value"), ValueText(Value));
        Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    return Rows;
}

TSharedPtr<FJsonObject> BuildMaterialInstanceRaw(UMaterialInstanceConstant* Instance)
{
    if (Instance == nullptr)
    {
        return nullptr;
    }
    TSharedPtr<FJsonObject> Raw = MakeRawEnvelope(Instance, TEXT("material_instance"));
    Raw->SetArrayField(TEXT("props"), ExportEditableProps(Instance));

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetArrayField(TEXT("scalar"), ParameterRows<FScalarParameterValue>(Instance->ScalarParameterValues, TEXT("float"), [](const FScalarParameterValue& Value) { return FString::SanitizeFloat(Value.ParameterValue); }));
    Params->SetArrayField(TEXT("vector"), ParameterRows<FVectorParameterValue>(Instance->VectorParameterValues, TEXT("FLinearColor"), [](const FVectorParameterValue& Value) { return Value.ParameterValue.ToString(); }));
    Params->SetArrayField(TEXT("texture"), ParameterRows<FTextureParameterValue>(Instance->TextureParameterValues, TEXT("UTexture"), [](const FTextureParameterValue& Value) { return Value.ParameterValue ? Value.ParameterValue->GetPathName() : TEXT("None"); }));
    Params->SetArrayField(TEXT("runtime_virtual_texture"), ParameterRows<FRuntimeVirtualTextureParameterValue>(Instance->RuntimeVirtualTextureParameterValues, TEXT("URuntimeVirtualTexture"), [](const FRuntimeVirtualTextureParameterValue& Value) { return Value.ParameterValue ? Value.ParameterValue->GetPathName() : TEXT("None"); }));

    TArray<TSharedPtr<FJsonValue>> Switches;
    TArray<TSharedPtr<FJsonValue>> Masks;
    const FStaticParameterSet& StaticParameters = Instance->GetStaticParameters();
    for (const FStaticSwitchParameter& Switch : StaticParameters.StaticSwitchParameters)
    {
        if (!Switch.bOverride)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Switch.ParameterInfo.Name.ToString());
        Row->SetStringField(TEXT("type"), TEXT("bool"));
        Row->SetStringField(TEXT("value"), Switch.Value ? TEXT("True") : TEXT("False"));
        Switches.Add(MakeShared<FJsonValueObject>(Row));
    }
    for (const FStaticComponentMaskParameter& Mask : StaticParameters.EditorOnly.StaticComponentMaskParameters)
    {
        if (!Mask.bOverride)
        {
            continue;
        }
        TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("name"), Mask.ParameterInfo.Name.ToString());
        Row->SetStringField(TEXT("type"), TEXT("FStaticComponentMask"));
        Row->SetStringField(TEXT("value"), FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"), Mask.R ? TEXT("True") : TEXT("False"), Mask.G ? TEXT("True") : TEXT("False"), Mask.B ? TEXT("True") : TEXT("False"), Mask.A ? TEXT("True") : TEXT("False")));
        Masks.Add(MakeShared<FJsonValueObject>(Row));
    }
    Params->SetArrayField(TEXT("switch"), Switches);
    Params->SetArrayField(TEXT("component_mask"), Masks);
    Raw->SetObjectField(TEXT("instance"), Params);
    return Raw;
}
}
