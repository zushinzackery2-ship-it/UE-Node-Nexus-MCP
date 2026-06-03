#include "UeNodeNexusBridgeWireGraph.h"

namespace UeNodeNexusBridge
{
FString StripWireGraphNodePrefix(const FString& Alias)
{
    return Alias.StartsWith(TEXT("N")) ? Alias.Mid(1) : Alias;
}

FString ShortWireGraphTypeName(FString TypeName)
{
    TypeName.RemoveFromStart(TEXT("MaterialExpression"));
    TypeName.RemoveFromStart(TEXT("K2Node_"));
    TypeName.RemoveFromStart(TEXT("EdGraphNode_"));

    static const TMap<FString, FString> Aliases = {
        {TEXT("ScalarParameter"), TEXT("SP")},
        {TEXT("TextureObjectParameter"), TEXT("TOP")},
        {TEXT("TextureSampleParameter2D"), TEXT("TSP2")},
        {TEXT("TextureSample"), TEXT("Tex")},
        {TEXT("MaterialFunctionCall"), TEXT("Fn")},
        {TEXT("Multiply"), TEXT("Mul")},
        {TEXT("Clamp"), TEXT("Cl")},
        {TEXT("Power"), TEXT("Pow")},
        {TEXT("OneMinus"), TEXT("1-")},
        {TEXT("Desaturation"), TEXT("Desat")},
        {TEXT("ComponentMask"), TEXT("Msk")},
        {TEXT("MakeMaterialAttributes"), TEXT("MakeMA")},
        {TEXT("SetMaterialAttributes"), TEXT("SetMA")},
        {TEXT("BlendMaterialAttributes"), TEXT("BlendMA")},
        {TEXT("NamedRerouteDeclaration"), TEXT("NRD")},
        {TEXT("NamedRerouteUsage"), TEXT("NRU")},
        {TEXT("Reroute"), TEXT("Reroute")},
        {TEXT("StaticBool"), TEXT("SB")},

        {TEXT("CallFunction"), TEXT("CF")},
        {TEXT("VariableGet"), TEXT("VG")},
        {TEXT("VariableSet"), TEXT("VS")},
        {TEXT("IfThenElse"), TEXT("If")},
        {TEXT("DynamicCast"), TEXT("Cast")},
        {TEXT("Event"), TEXT("Ev")},
        {TEXT("CustomEvent"), TEXT("CEvt")},
        {TEXT("FunctionEntry"), TEXT("FnE")},
        {TEXT("FunctionResult"), TEXT("FnR")},
        {TEXT("Knot"), TEXT("K")},
        {TEXT("Self"), TEXT("Self")},
        {TEXT("MakeArray"), TEXT("Arr")},
        {TEXT("MakeStruct"), TEXT("Strc")},
        {TEXT("BreakStruct"), TEXT("BrkS")},
        {TEXT("Timeline"), TEXT("TL")},
        {TEXT("Sequence"), TEXT("Seq")},
        {TEXT("MacroInstance"), TEXT("Mac")},
        {TEXT("CommutativeAssociativeBinaryOperator"), TEXT("BinOp")},
        {TEXT("SwitchEnum"), TEXT("SwE")},
        {TEXT("SwitchInteger"), TEXT("SwI")},
        {TEXT("SwitchString"), TEXT("SwS")},
        {TEXT("Select"), TEXT("Sel")},
        {TEXT("ForEachLoop"), TEXT("ForEa")},
        {TEXT("ForLoop"), TEXT("For")},
        {TEXT("WhileLoop"), TEXT("While")},
        {TEXT("AssignmentStatement"), TEXT("Asn")},
        {TEXT("EnhancedInputAction"), TEXT("EIA")},
    };

    if (const FString* Alias = Aliases.Find(TypeName))
    {
        return *Alias;
    }
    return TypeName;
}

FString ShortWireGraphPinName(FString PinName)
{
    PinName.ReplaceInline(TEXT(" "), TEXT(""));
    PinName.ReplaceInline(TEXT("(S)"), TEXT(""));
    PinName.ReplaceInline(TEXT("(SB)"), TEXT(""));
    PinName.ReplaceInline(TEXT("(T2d)"), TEXT(""));
    PinName.ReplaceInline(TEXT("(MA)"), TEXT(""));
    PinName.ReplaceInline(TEXT("(V3)"), TEXT(""));

    static const TMap<FString, FString> Aliases = {
        {TEXT("0"), TEXT("o")},
        {TEXT("Output"), TEXT("o")},
        {TEXT("OutputPin"), TEXT("o")},
        {TEXT("Result"), TEXT("r")},
        {TEXT("Input"), TEXT("i")},
        {TEXT("InputPin"), TEXT("i")},
        {TEXT("BaseColor"), TEXT("BC")},
        {TEXT("Metallic"), TEXT("M")},
        {TEXT("Roughness"), TEXT("Rgh")},
        {TEXT("Normal"), TEXT("N")},
        {TEXT("AmbientOcclusion"), TEXT("AO")},
        {TEXT("MaterialAttributes"), TEXT("MA")},
        {TEXT("材质属性"), TEXT("MA")},
        {TEXT("环境光遮挡"), TEXT("AO")},
        {TEXT("粗糙度"), TEXT("Rgh")},
        {TEXT("三维映射贴图"), TEXT("Tex3")},
        {TEXT("U平铺"), TEXT("U")},
        {TEXT("V平铺"), TEXT("V")},
        {TEXT("旋转"), TEXT("Rot")}
    };

    if (const FString* Alias = Aliases.Find(PinName))
    {
        return *Alias;
    }
    return PinName;
}

FString ShortWireGraphNodeLabel(const FString& Label)
{
    int32 OpenIndex = INDEX_NONE;
    int32 CloseIndex = INDEX_NONE;
    if (Label.FindChar(TEXT('('), OpenIndex) && Label.FindLastChar(TEXT(')'), CloseIndex) && CloseIndex > OpenIndex)
    {
        const FString TypeName = Label.Left(OpenIndex);
        const FString DisplayName = Label.Mid(OpenIndex + 1, CloseIndex - OpenIndex - 1);
        return ShortWireGraphTypeName(TypeName) + TEXT(":") + DisplayName;
    }
    return ShortWireGraphTypeName(Label);
}
}
