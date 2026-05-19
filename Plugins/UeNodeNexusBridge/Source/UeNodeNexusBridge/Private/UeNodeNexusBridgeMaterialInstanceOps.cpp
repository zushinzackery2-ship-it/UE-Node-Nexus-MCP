#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static UMaterialInstanceConstant* LoadMaterialInstance(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UMaterialInstanceConstant* Instance = LoadObject<UMaterialInstanceConstant>(nullptr, *AssetPath);
    if (Instance == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Material instance constant could not be loaded")));
        return nullptr;
    }

    return Instance;
}

static TSharedPtr<FJsonObject> VectorToJson(const FLinearColor& Value)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("r"), Value.R);
    Json->SetNumberField(TEXT("g"), Value.G);
    Json->SetNumberField(TEXT("b"), Value.B);
    Json->SetNumberField(TEXT("a"), Value.A);
    return Json;
}

static TSharedPtr<FJsonValue> MakeParamValue(UMaterialInstanceConstant* Instance, const FString& Type, const FName& Name)
{
    if (Type == TEXT("scalar"))
    {
        const float Value = UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(Instance, Name);
        return MakeShared<FJsonValueNumber>(Value);
    }
    if (Type == TEXT("vector"))
    {
        const FLinearColor Value = UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(Instance, Name);
        return MakeShared<FJsonValueObject>(VectorToJson(Value));
    }
    if (Type == TEXT("texture"))
    {
        UTexture* Texture = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(Instance, Name);
        return MakeShared<FJsonValueString>(Texture ? Texture->GetPathName() : FString());
    }
    if (Type == TEXT("static_switch"))
    {
        const bool bValue = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(Instance, Name);
        return MakeShared<FJsonValueBoolean>(bValue);
    }
    return MakeShared<FJsonValueString>(FString());
}

static TSharedPtr<FJsonObject> MakeParamJson(UMaterialInstanceConstant* Instance, const FString& Type, const FName& Name)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("name"), Name.ToString());
    Json->SetStringField(TEXT("type"), Type);
    Json->SetField(TEXT("value"), MakeParamValue(Instance, Type, Name));
    return Json;
}

static TSharedPtr<FJsonValue> MakeParamRow(UMaterialInstanceConstant* Instance, const FString& Type, const FName& Name)
{
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Type));
    Row.Add(MakeShared<FJsonValueString>(Name.ToString()));
    Row.Add(MakeParamValue(Instance, Type, Name));
    return MakeShared<FJsonValueArray>(Row);
}

static void AddParameterNames(UMaterialInstanceConstant* Instance, const FString& Type, bool bCompact, TArray<TSharedPtr<FJsonValue>>& Items)
{
    TArray<FName> Names;
    if (Type == TEXT("scalar"))
    {
        UMaterialEditingLibrary::GetScalarParameterNames(Instance, Names);
    }
    else if (Type == TEXT("vector"))
    {
        UMaterialEditingLibrary::GetVectorParameterNames(Instance, Names);
    }
    else if (Type == TEXT("texture"))
    {
        UMaterialEditingLibrary::GetTextureParameterNames(Instance, Names);
    }
    else if (Type == TEXT("static_switch"))
    {
        UMaterialEditingLibrary::GetStaticSwitchParameterNames(Instance, Names);
    }

    for (const FName& Name : Names)
    {
        if (bCompact)
        {
            Items.Add(MakeParamRow(Instance, Type, Name));
        }
        else
        {
            Items.Add(MakeShared<FJsonValueObject>(MakeParamJson(Instance, Type, Name)));
        }
    }
}

TSharedPtr<FJsonObject> HandleMaterialInstanceParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UMaterialInstanceConstant* Instance = LoadMaterialInstance(Payload, EarlyResponse, Operation, RequestId);
    if (Instance == nullptr)
    {
        return EarlyResponse;
    }

    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
    AddParameterNames(Instance, TEXT("scalar"), bCompact, Items);
    AddParameterNames(Instance, TEXT("vector"), bCompact, Items);
    AddParameterNames(Instance, TEXT("texture"), bCompact, Items);
    AddParameterNames(Instance, TEXT("static_switch"), bCompact, Items);

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Instance->GetPathName());
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("material_instance_params_compact"));
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("type")),
            MakeShared<FJsonValueString>(TEXT("name")),
            MakeShared<FJsonValueString>(TEXT("value"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool ApplyScalar(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    double Value = 0.0;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetNumberField(TEXT("value"), Value))
    {
        return false;
    }
    return UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, FName(*Name), static_cast<float>(Value));
}

static bool ApplyVector(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetObjectField(TEXT("value"), Value) || Value == nullptr)
    {
        return false;
    }

    double R = 0.0;
    double G = 0.0;
    double B = 0.0;
    double A = 1.0;
    (*Value)->TryGetNumberField(TEXT("r"), R);
    (*Value)->TryGetNumberField(TEXT("g"), G);
    (*Value)->TryGetNumberField(TEXT("b"), B);
    (*Value)->TryGetNumberField(TEXT("a"), A);
    return UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, FName(*Name), FLinearColor(R, G, B, A));
}

static bool ApplyTexture(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    FString Value;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetStringField(TEXT("value"), Value))
    {
        return false;
    }

    UTexture* Texture = LoadObject<UTexture>(nullptr, *Value);
    return Texture != nullptr && UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, FName(*Name), Texture);
}

static bool ApplyStaticSwitch(UMaterialInstanceConstant* Instance, const TSharedPtr<FJsonObject>& Param)
{
    FString Name;
    bool bValue = false;
    if (!Param->TryGetStringField(TEXT("name"), Name) || !Param->TryGetBoolField(TEXT("value"), bValue))
    {
        return false;
    }
    return UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, FName(*Name), bValue);
}

TSharedPtr<FJsonObject> HandleMaterialInstanceParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UMaterialInstanceConstant* Instance = LoadMaterialInstance(Payload, EarlyResponse, Operation, RequestId);
    if (Instance == nullptr)
    {
        return EarlyResponse;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
    if (!Payload->TryGetArrayField(TEXT("params"), Params) || Params == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("params must be an array")));
        return Response;
    }

    int32 Changed = 0;
    int32 Planned = 0;
    for (const TSharedPtr<FJsonValue>& Value : *Params)
    {
        TSharedPtr<FJsonObject> Param = Value->AsObject();
        FString Type;
        if (!Param.IsValid() || !Param->TryGetStringField(TEXT("type"), Type))
        {
            continue;
        }
        ++Planned;

        bool bChanged = false;
        if (!bDryRun)
        {
            if (Type == TEXT("scalar"))
            {
                bChanged = ApplyScalar(Instance, Param);
            }
            else if (Type == TEXT("vector"))
            {
                bChanged = ApplyVector(Instance, Param);
            }
            else if (Type == TEXT("texture"))
            {
                bChanged = ApplyTexture(Instance, Param);
            }
            else if (Type == TEXT("static_switch"))
            {
                bChanged = ApplyStaticSwitch(Instance, Param);
            }
        }

        if (bChanged)
        {
            ++Changed;
        }
    }

    if (!bDryRun)
    {
        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
        Instance->MarkPackageDirty();
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), !bDryRun);
    Data->SetBoolField(TEXT("changed"), Changed > 0);
    Data->SetNumberField(TEXT("planned_count"), Planned);
    Data->SetNumberField(TEXT("changed_count"), Changed);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
