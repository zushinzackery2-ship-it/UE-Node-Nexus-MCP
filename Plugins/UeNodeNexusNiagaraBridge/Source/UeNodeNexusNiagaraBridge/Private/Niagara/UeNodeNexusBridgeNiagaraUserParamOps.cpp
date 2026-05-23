#include "UeNodeNexusNiagaraOps.h"

#include "UeNodeNexusBridgeNiagaraHelpers.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static FString NormalizeUserParamName(const FString& Name)
{
    return Name.StartsWith(TEXT("User.")) ? Name : FString(TEXT("User.")) + Name;
}

static FNiagaraTypeDefinition TypeFromString(const FString& TypeName)
{
    if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetFloatDef();
    }
    if (TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetIntDef();
    }
    if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetBoolDef();
    }
    if (TypeName.Equals(TEXT("vec2"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("vector2"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec2Def();
    }
    if (TypeName.Equals(TEXT("vec3"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec3Def();
    }
    if (TypeName.Equals(TEXT("position"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetPositionDef();
    }
    if (TypeName.Equals(TEXT("vec4"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("vector4"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetVec4Def();
    }
    if (TypeName.Equals(TEXT("color"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition::GetColorDef();
    }
    if (TypeName.Equals(TEXT("material"), ESearchCase::IgnoreCase))
    {
        return FNiagaraTypeDefinition(UMaterialInterface::StaticClass());
    }
    return FNiagaraTypeDefinition();
}

static bool ReadVec4(const TSharedPtr<FJsonObject>& Param, double& X, double& Y, double& Z, double& W)
{
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Param->TryGetObjectField(TEXT("value"), Value) || Value == nullptr)
    {
        return false;
    }
    (*Value)->TryGetNumberField(TEXT("x"), X);
    (*Value)->TryGetNumberField(TEXT("y"), Y);
    (*Value)->TryGetNumberField(TEXT("z"), Z);
    (*Value)->TryGetNumberField(TEXT("w"), W);
    (*Value)->TryGetNumberField(TEXT("r"), X);
    (*Value)->TryGetNumberField(TEXT("g"), Y);
    (*Value)->TryGetNumberField(TEXT("b"), Z);
    (*Value)->TryGetNumberField(TEXT("a"), W);
    return true;
}

static bool ApplyUserParam(FNiagaraParameterStore& Store, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    FString TypeName;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetStringField(TEXT("type"), TypeName))
    {
        return false;
    }

    const FNiagaraTypeDefinition Type = TypeFromString(TypeName);
    if (!Type.IsValid())
    {
        return false;
    }

    bool bAdd = false;
    Param->TryGetBoolField(TEXT("add_if_missing"), bAdd);
    FNiagaraVariable Variable(Type, FName(*NormalizeUserParamName(Name)));
    if (Type == FNiagaraTypeDefinition::GetFloatDef())
    {
        double Value = 0.0;
        return Param->TryGetNumberField(TEXT("value"), Value) && Store.SetParameterValue<float>(static_cast<float>(Value), Variable, bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetIntDef())
    {
        double Value = 0.0;
        return Param->TryGetNumberField(TEXT("value"), Value) && Store.SetParameterValue<int32>(static_cast<int32>(Value), Variable, bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetBoolDef())
    {
        bool bValue = false;
        return Param->TryGetBoolField(TEXT("value"), bValue) && Store.SetParameterValue<FNiagaraBool>(FNiagaraBool(bValue), Variable, bAdd);
    }

    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 1.0;
    if (Type == FNiagaraTypeDefinition::GetVec2Def())
    {
        return ReadVec4(Param, X, Y, Z, W) && Store.SetParameterValue<FVector2f>(FVector2f(X, Y), Variable, bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetVec3Def())
    {
        return ReadVec4(Param, X, Y, Z, W) && Store.SetParameterValue<FVector3f>(FVector3f(X, Y, Z), Variable, bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetPositionDef())
    {
        return ReadVec4(Param, X, Y, Z, W) && Store.SetPositionParameterValue(FVector(X, Y, Z), Variable.GetName(), bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetVec4Def())
    {
        return ReadVec4(Param, X, Y, Z, W) && Store.SetParameterValue<FVector4f>(FVector4f(X, Y, Z, W), Variable, bAdd);
    }
    if (Type == FNiagaraTypeDefinition::GetColorDef())
    {
        return ReadVec4(Param, X, Y, Z, W) && Store.SetParameterValue<FLinearColor>(FLinearColor(X, Y, Z, W), Variable, bAdd);
    }
    if (Type.GetClass() != nullptr)
    {
        FString ValuePath;
        UObject* Object = Param->TryGetStringField(TEXT("value"), ValuePath) ? LoadObject<UObject>(nullptr, *ValuePath) : nullptr;
        if (Object == nullptr || !Object->IsA(Type.GetClass()))
        {
            return false;
        }
        if (Store.IndexOf(Variable) == INDEX_NONE && bAdd)
        {
            Store.AddParameter(Variable);
        }
        Store.SetUObject(Object, Variable);
        return Store.IndexOf(Variable) != INDEX_NONE;
    }
    return false;
}

TSharedPtr<FJsonObject> HandleNiagaraUserParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UNiagaraSystem* System = LoadNiagaraSystemFromPayload(Payload, Operation, RequestId, EarlyResponse);
    if (System == nullptr)
    {
        return EarlyResponse;
    }

    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), UeNodeNexusBridge::MakeError(TEXT("invalid_request"), TEXT("params must be an array")));
        return Response;
    }

    bool bDryRun = true;
    bool bSave = false;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    Payload->TryGetBoolField(TEXT("save"), bSave);

    int32 Planned = 0;
    int32 Changed = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        TSharedPtr<FJsonObject> Param = Value->AsObject();
        if (!Param.IsValid())
        {
            continue;
        }
        ++Planned;
        if (!bDryRun && ApplyUserParam(System->GetExposedParameters(), Param))
        {
            ++Changed;
        }
    }

    if (!bDryRun && Changed > 0)
    {
        System->Modify();
        System->MarkPackageDirty();
        System->PostEditChange();
    }
    const bool bSaved = bSave && Changed > 0 && SaveAssetPackage(System);

    TSharedPtr<FJsonObject> Data = MakeNiagaraAssetSummaryData(System);
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);
    Data->SetBoolField(TEXT("saved"), bSaved);
    Data->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    Data->SetNumberField(TEXT("enabled_emitter_count"), CountEnabledNiagaraEmitters(System));
    Data->SetNumberField(TEXT("renderer_count"), CountNiagaraRenderers(System));
    Data->SetNumberField(TEXT("enabled_renderer_count"), CountEnabledNiagaraRenderers(System));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    TArray<TSharedPtr<FJsonValue>> Warnings;
    AppendNiagaraEmptySystemWarning(System, Warnings);
    if (Warnings.Num() > 0)
    {
        Response->SetArrayField(TEXT("warnings"), Warnings);
    }
    return Response;
}
}
