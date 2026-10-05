#include "Material/ControlFlow/NexusMaterialControlFlow.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionExecBegin.h"
#include "Materials/MaterialExpressionExecEnd.h"
#include "Transcode/UeNodeNexusBridgeTranscode.h"
#include "Transcode/UeNodeNexusBridgeTranscodeMaterialApply.h"

namespace UeNodeNexusBridge::Transcode
{
UMaterialExpression* FindMaterialControlFlowRoot(UObject* Owner, UClass* Class)
{
    UMaterial* Material = Cast<UMaterial>(Owner);
    if (Material == nullptr)
    {
        return nullptr;
    }
    if (Class == UMaterialExpressionExecBegin::StaticClass())
    {
        return Material->GetExpressionExecBegin();
    }
    if (Class == UMaterialExpressionExecEnd::StaticClass())
    {
        return Material->GetExpressionExecEnd();
    }
    return nullptr;
}

void AppendMaterialControlFlowPins(UMaterialExpression* Expression,
    TArray<TSharedPtr<FJsonValue>>& Inputs, TArray<TSharedPtr<FJsonValue>>& Outputs)
{
    if (Expression->HasExecInput())
    {
        Inputs.Add(MakeShared<FJsonValueString>(TEXT("execute")));
    }
    TArray<FExpressionExecOutputEntry> ExecOutputs;
    Expression->GetExecOutputs(ExecOutputs);
    for (const FExpressionExecOutputEntry& Entry : ExecOutputs)
    {
        Outputs.Add(MakeShared<FJsonValueString>(Entry.Name.ToString()));
    }
}

void AppendMaterialControlFlowLinks(UMaterialExpression* Expression,
    TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, TArray<TSharedPtr<FJsonValue>>& Links)
{
    TArray<FExpressionExecOutputEntry> ExecOutputs;
    Expression->GetExecOutputs(ExecOutputs);
    for (int32 Index = 0; Index < ExecOutputs.Num(); ++Index)
    {
        UMaterialExpression* Target = ExecOutputs[Index].Output->GetExpression();
        if (Target == nullptr)
        {
            continue;
        }
        int32 InputCount = 0;
        for (FExpressionInputIterator It(Target); It; ++It)
        {
            InputCount = It.Index + 1;
        }
        auto Link = MakeShared<FJsonObject>();
        Link->SetStringField(TEXT("from"), MaterialExpressionKey(Expression, Expressions));
        Link->SetNumberField(TEXT("from_out"), Expression->GetOutputs().Num() + Index);
        Link->SetStringField(TEXT("to"), MaterialExpressionKey(Target, Expressions));
        Link->SetNumberField(TEXT("to_in"), InputCount);
        Links.Add(MakeShared<FJsonValueObject>(Link));
    }
}

bool ApplyMaterialControlFlowLink(UObject* Owner, UMaterialExpression* From,
    UMaterialExpression* To, const FString& FromPin, bool bConnect, FString& OutError)
{
    if (To == nullptr || !To->HasExecInput())
    {
        OutError = TEXT("Target does not have a material execution input");
        return false;
    }
    if (!bConnect)
    {
        for (const TObjectPtr<UMaterialExpression>& Expression : OwnerExpressions(Owner))
        {
            TArray<FExpressionExecOutputEntry> Outputs;
            Expression->GetExecOutputs(Outputs);
            for (const FExpressionExecOutputEntry& Entry : Outputs)
            {
                if (Entry.Output->GetExpression() == To)
                {
                    Expression->Modify();
                    Entry.Output->Connect(nullptr);
                }
            }
        }
        return true;
    }
    if (From == nullptr)
    {
        OutError = TEXT("Material execution source is missing");
        return false;
    }
    TArray<FExpressionExecOutputEntry> Outputs;
    From->GetExecOutputs(Outputs);
    const int32 NumericIndex = FromPin.IsNumeric() ? FCString::Atoi(*FromPin) - From->GetOutputs().Num() : INDEX_NONE;
    for (int32 Index = 0; Index < Outputs.Num(); ++Index)
    {
        if (Index == NumericIndex || Outputs[Index].Name.ToString().Equals(FromPin, ESearchCase::IgnoreCase))
        {
            From->Modify();
            Outputs[Index].Output->Connect(To);
            return true;
        }
    }
    OutError = FString::Printf(TEXT("Material execution output is missing: %s"), *FromPin);
    return false;
}

void BindMaterialControlFlowRoot(UObject* Owner, UMaterialExpression* Expression)
{
    UMaterial* Material = Cast<UMaterial>(Owner);
    if (Material == nullptr)
    {
        return;
    }
    if (auto* Begin = Cast<UMaterialExpressionExecBegin>(Expression))
    {
        Material->GetExpressionCollection().ExpressionExecBegin = Begin;
    }
    if (auto* End = Cast<UMaterialExpressionExecEnd>(Expression))
    {
        Material->GetExpressionCollection().ExpressionExecEnd = End;
    }
}

void DisconnectMaterialControlFlowExpression(UObject* Owner, UMaterialExpression* Expression)
{
    FString Error;
    if (Expression->HasExecInput())
    {
        ApplyMaterialControlFlowLink(Owner, nullptr, Expression, TEXT(""), false, Error);
    }
    TArray<FExpressionExecOutputEntry> Outputs;
    Expression->GetExecOutputs(Outputs);
    Expression->Modify();
    for (const FExpressionExecOutputEntry& Entry : Outputs)
    {
        Entry.Output->Connect(nullptr);
    }
}
}
