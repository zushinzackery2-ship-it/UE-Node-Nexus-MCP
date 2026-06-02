#include "UeNodeNexusBridgeMaterialInterfaceHelpers.h"

#include "Components/MeshComponent.h"
#include "Engine/Texture.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> MakeMaterialInterfaceError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    return MakeOperationError(Operation, RequestId, Code, Message);
}

static TSharedPtr<FJsonObject> ColorToJson(const FLinearColor& Color)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("r"), Color.R);
    Json->SetNumberField(TEXT("g"), Color.G);
    Json->SetNumberField(TEXT("b"), Color.B);
    Json->SetNumberField(TEXT("a"), Color.A);
    return Json;
}

UMaterialInterface* ResolveMaterialInterfaceFromPayload(const TSharedPtr<FJsonObject>& Payload)
{
    FString AssetPath;
    Payload->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (AssetPath.IsEmpty())
    {
        Payload->TryGetStringField(TEXT("material_path"), AssetPath);
    }
    return Cast<UMaterialInterface>(ResolveObjectByPath(AssetPath));
}

UMaterialInterface* ResolveComponentSlotMaterial(const TSharedPtr<FJsonObject>& Payload, UMeshComponent*& OutComponent, int32& OutSlotIndex)
{
    FString ComponentPath;
    double SlotNumber = 0.0;
    Payload->TryGetStringField(TEXT("component_path"), ComponentPath);
    OutComponent = Cast<UMeshComponent>(ResolveObjectByPath(ComponentPath));
    if (OutComponent == nullptr || !Payload->TryGetNumberField(TEXT("slot_index"), SlotNumber))
    {
        return nullptr;
    }

    OutSlotIndex = static_cast<int32>(SlotNumber);
    if (OutSlotIndex < 0 || OutSlotIndex >= OutComponent->GetNumMaterials())
    {
        return nullptr;
    }
    return OutComponent->GetMaterial(OutSlotIndex);
}

static TSharedPtr<FJsonObject> MakeChainItem(UMaterialInterface* MaterialInterface, int32 Depth)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetNumberField(TEXT("depth"), Depth);
    Json->SetStringField(TEXT("material_path"), MaterialInterface ? MaterialInterface->GetPathName() : FString());
    Json->SetStringField(TEXT("class_path"), MaterialInterface && MaterialInterface->GetClass() ? MaterialInterface->GetClass()->GetPathName() : FString());
    Json->SetBoolField(TEXT("is_instance"), MaterialInterface && MaterialInterface->IsA<UMaterialInstance>());
    Json->SetBoolField(TEXT("is_dynamic"), MaterialInterface && MaterialInterface->IsA<UMaterialInstanceDynamic>());
    if (UMaterialInstance* Instance = Cast<UMaterialInstance>(MaterialInterface))
    {
        Json->SetStringField(TEXT("parent_path"), Instance->Parent ? Instance->Parent->GetPathName() : FString());
    }
    return Json;
}

static TArray<TSharedPtr<FJsonValue>> BuildMaterialParentChain(UMaterialInterface* MaterialInterface)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    TSet<UMaterialInterface*> Seen;
    UMaterialInterface* Current = MaterialInterface;
    int32 Depth = 0;
    while (Current != nullptr && !Seen.Contains(Current))
    {
        Seen.Add(Current);
        Items.Add(MakeShared<FJsonValueObject>(MakeChainItem(Current, Depth++)));
        UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
        Current = Instance ? Instance->Parent : nullptr;
    }
    return Items;
}

static void AddScalarRows(UMaterialInterface* MaterialInterface, TArray<TSharedPtr<FJsonValue>>& Items)
{
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    MaterialInterface->GetAllScalarParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        float Value = 0.0f;
        MaterialInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Value);
        Items.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("scalar")),
            MakeShared<FJsonValueString>(Info.Name.ToString()),
            MakeShared<FJsonValueNumber>(Value)
        }));
    }
}

static void AddVectorRows(UMaterialInterface* MaterialInterface, TArray<TSharedPtr<FJsonValue>>& Items)
{
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    MaterialInterface->GetAllVectorParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        FLinearColor Value;
        MaterialInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Value);
        Items.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("vector")),
            MakeShared<FJsonValueString>(Info.Name.ToString()),
            MakeShared<FJsonValueObject>(ColorToJson(Value))
        }));
    }
}

static void AddTextureRows(UMaterialInterface* MaterialInterface, TArray<TSharedPtr<FJsonValue>>& Items)
{
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    MaterialInterface->GetAllTextureParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        UTexture* Value = nullptr;
        MaterialInterface->GetTextureParameterValue(FHashedMaterialParameterInfo(Info), Value);
        Items.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("texture")),
            MakeShared<FJsonValueString>(Info.Name.ToString()),
            MakeShared<FJsonValueString>(Value ? Value->GetPathName() : FString())
        }));
    }
}

static void AddParameterRows(UMaterialInterface* MaterialInterface, TArray<TSharedPtr<FJsonValue>>& Items)
{
    AddScalarRows(MaterialInterface, Items);
    AddVectorRows(MaterialInterface, Items);
    AddTextureRows(MaterialInterface, Items);
#if WITH_EDITORONLY_DATA
    TArray<FMaterialParameterInfo> Infos;
    TArray<FGuid> Ids;
    MaterialInterface->GetAllStaticSwitchParameterInfo(Infos, Ids);
    for (const FMaterialParameterInfo& Info : Infos)
    {
        bool bValue = false;
        FGuid ExpressionGuid;
        MaterialInterface->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(Info), bValue, ExpressionGuid);
        Items.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
            MakeShared<FJsonValueString>(TEXT("static_switch")),
            MakeShared<FJsonValueString>(Info.Name.ToString()),
            MakeShared<FJsonValueBoolean>(bValue)
        }));
    }
#endif
}

TSharedPtr<FJsonObject> BuildMaterialResolveData(UMaterialInterface* MaterialInterface, bool bIncludeParams)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    UMaterial* RootMaterial = MaterialInterface ? MaterialInterface->GetMaterial() : nullptr;
    Data->SetStringField(TEXT("material_path"), MaterialInterface ? MaterialInterface->GetPathName() : FString());
    Data->SetStringField(TEXT("material_class"), MaterialInterface && MaterialInterface->GetClass() ? MaterialInterface->GetClass()->GetPathName() : FString());
    Data->SetStringField(TEXT("root_material_path"), RootMaterial ? RootMaterial->GetPathName() : FString());
    Data->SetArrayField(TEXT("parent_chain"), BuildMaterialParentChain(MaterialInterface));
    if (bIncludeParams && MaterialInterface != nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> Params;
        AddParameterRows(MaterialInterface, Params);
        Data->SetStringField(TEXT("params_format"), TEXT("material_interface_params_compact"));
        Data->SetArrayField(TEXT("param_columns"), {
            MakeShared<FJsonValueString>(TEXT("type")),
            MakeShared<FJsonValueString>(TEXT("name")),
            MakeShared<FJsonValueString>(TEXT("value"))
        });
        Data->SetArrayField(TEXT("params"), Params);
        Data->SetNumberField(TEXT("param_count"), Params.Num());
    }
    return Data;
}

bool MaterialMatchesQuery(UMaterialInterface* Candidate, UMaterialInterface* Query)
{
    if (Candidate == nullptr || Query == nullptr)
    {
        return false;
    }
    if (Candidate == Query || Candidate->GetMaterial() == Query->GetMaterial())
    {
        return true;
    }
    for (UMaterialInterface* Current = Candidate; Current != nullptr;)
    {
        if (Current == Query)
        {
            return true;
        }
        UMaterialInstance* Instance = Cast<UMaterialInstance>(Current);
        Current = Instance ? Instance->Parent : nullptr;
    }
    return false;
}

void ApplyDynamicMaterialParam(UMaterialInstanceDynamic* Mid, const TSharedPtr<FJsonObject>& Param, bool& bApplied)
{
    FString Type;
    FString Name;
    if (Mid == nullptr || !Param.IsValid() || !Param->TryGetStringField(TEXT("type"), Type) || !Param->TryGetStringField(TEXT("name"), Name))
    {
        return;
    }

    if (Type == TEXT("scalar"))
    {
        double Value = 0.0;
        if (Param->TryGetNumberField(TEXT("value"), Value))
        {
            Mid->SetScalarParameterValue(FName(*Name), static_cast<float>(Value));
            bApplied = true;
        }
        return;
    }

    if (Type == TEXT("texture"))
    {
        FString Path;
        if (Param->TryGetStringField(TEXT("value"), Path))
        {
            UTexture* Texture = Cast<UTexture>(ResolveObjectByPath(Path));
            if (Texture != nullptr)
            {
                Mid->SetTextureParameterValue(FName(*Name), Texture);
                bApplied = true;
            }
        }
        return;
    }

    const TSharedPtr<FJsonObject>* Value = nullptr;
    if (Type == TEXT("vector") && Param->TryGetObjectField(TEXT("value"), Value) && Value != nullptr)
    {
        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
        double A = 1.0;
        (*Value)->TryGetNumberField(TEXT("r"), R);
        (*Value)->TryGetNumberField(TEXT("g"), G);
        (*Value)->TryGetNumberField(TEXT("b"), B);
        (*Value)->TryGetNumberField(TEXT("a"), A);
        Mid->SetVectorParameterValue(FName(*Name), FLinearColor(R, G, B, A));
        bApplied = true;
    }
}
}
