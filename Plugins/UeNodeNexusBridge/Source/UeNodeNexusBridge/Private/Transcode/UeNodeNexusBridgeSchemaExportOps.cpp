#include "UeNodeNexusBridgeTranscode.h"
#include "Schema/NexusSchema.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MaterialExpressionIO.h"
#include "Misc/DateTime.h"
#include "UObject/UObjectIterator.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
using namespace Transcode;

static TSharedPtr<FJsonObject> ClassPropsJson(UClass* Class)
{
    TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
    UObject* Cdo = Class->GetDefaultObject();
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        if (IsEditableProperty(*It))
        {
            Props->SetObjectField(It->GetName(), PropertySchemaJson(*It, Cdo));
        }
    }
    return Props;
}

static bool IsConcreteClass(UClass* Class)
{
    return Class != nullptr
        && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        && !Class->GetName().StartsWith(TEXT("SKEL_"))
        && !Class->GetName().StartsWith(TEXT("REINST_"));
}

static TSharedPtr<FJsonObject> MaterialExpressionClasses()
{
    TSharedPtr<FJsonObject> Classes = MakeShared<FJsonObject>();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (!IsConcreteClass(Class) || !Class->IsChildOf(UMaterialExpression::StaticClass()))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Class->GetPathName());
        Record->SetObjectField(TEXT("props"), ClassPropsJson(Class));
        UMaterialExpression* Cdo = Cast<UMaterialExpression>(Class->GetDefaultObject());
        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;
        if (Cdo != nullptr)
        {
            for (FExpressionInputIterator InputIt{ Cdo }; InputIt; ++InputIt)
            {
                Inputs.Add(MakeShared<FJsonValueString>(Cdo->GetInputName(InputIt.Index).ToString()));
            }
            for (const FExpressionOutput& Output : Cdo->GetOutputs())
            {
                Outputs.Add(MakeShared<FJsonValueString>(Output.OutputName.IsNone() ? FString() : Output.OutputName.ToString()));
            }
        }
        Record->SetArrayField(TEXT("inputs"), Inputs);
        Record->SetArrayField(TEXT("outputs"), Outputs);
        AddClassMetadata(Class, Record);
        Classes->SetObjectField(Class->GetPathName(), Record);
    }
    return Classes;
}

static TSharedPtr<FJsonObject> ClassFamily(TFunctionRef<bool(UClass*)> Filter)
{
    TSharedPtr<FJsonObject> Classes = MakeShared<FJsonObject>();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (!IsConcreteClass(Class) || !Filter(Class))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Class->GetPathName());
        Record->SetObjectField(TEXT("props"), ClassPropsJson(Class));
        AddClassMetadata(Class, Record);
        Classes->SetObjectField(Class->GetPathName(), Record);
    }
    return Classes;
}

static TSharedPtr<FJsonObject> MaterialFunctionSignatures()
{
    TSharedPtr<FJsonObject> Functions = MakeShared<FJsonObject>();
    TArray<FAssetData> Assets;
    FAssetRegistryModule::GetRegistry().GetAssetsByClass(UMaterialFunction::StaticClass()->GetClassPathName(), Assets, true);
    for (const FAssetData& AssetData : Assets)
    {
        if (!AssetData.IsAssetLoaded())
        {
            const auto Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
            Record->SetStringField(TEXT("coverage"), TEXT("context_required"));
            Record->SetField(TEXT("inputs"), MakeShared<FJsonValueNull>());
            Record->SetField(TEXT("outputs"), MakeShared<FJsonValueNull>());
            Functions->SetObjectField(AssetData.GetObjectPathString(), Record);
            continue;
        }
        UMaterialFunction* Function = Cast<UMaterialFunction>(AssetData.GetAsset());
        if (Function == nullptr)
        {
            continue;
        }
        TArray<FFunctionExpressionInput> Inputs;
        TArray<FFunctionExpressionOutput> Outputs;
        Function->GetInputsAndOutputs(Inputs, Outputs);
        TArray<TSharedPtr<FJsonValue>> InputRows;
        for (const FFunctionExpressionInput& Input : Inputs)
        {
            TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), Input.Input.InputName.ToString());
            Row->SetStringField(TEXT("type"), Input.ExpressionInput ? StaticEnum<EFunctionInputType>()->GetNameStringByValue(static_cast<int64>(Input.ExpressionInput->InputType.GetValue())) : FString());
            InputRows.Add(MakeShared<FJsonValueObject>(Row));
        }
        TArray<TSharedPtr<FJsonValue>> OutputRows;
        for (const FFunctionExpressionOutput& Output : Outputs)
        {
            TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), Output.Output.OutputName.ToString());
            OutputRows.Add(MakeShared<FJsonValueObject>(Row));
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetArrayField(TEXT("inputs"), InputRows);
        Record->SetArrayField(TEXT("outputs"), OutputRows);
        Functions->SetObjectField(Function->GetPathName(), Record);
    }
    return Functions;
}

static TSharedPtr<FJsonObject> RequestedFunctionSignatures(const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Records = MakeShared<FJsonObject>();
    const TArray<TSharedPtr<FJsonValue>>* Requests = nullptr;
    if (!Payload->TryGetArrayField(TEXT("functions"), Requests) || Requests == nullptr)
    {
        return Records;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Requests)
    {
        FString Reference;
        FString Owner;
        FString Name;
        if (!Value.IsValid() || !Value->TryGetString(Reference) || !Reference.Split(TEXT("."), &Owner, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
        {
            continue;
        }
        UClass* OwnerClass = Owner.StartsWith(TEXT("/")) ? LoadObject<UClass>(nullptr, *Owner) : UClass::TryFindTypeSlow<UClass>(Owner);
        UFunction* Function = OwnerClass ? OwnerClass->FindFunctionByName(FName(*Name)) : nullptr;
        if (Function != nullptr)
        {
            Records->SetObjectField(FString::Printf(TEXT("%s.%s"), *OwnerClass->GetPathName(), *Name), BuildFunctionSignatureRecord(Function));
        }
    }
    return Records;
}

TSharedPtr<FJsonObject> HandleSchemaExport(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString OutDir;
    if (!Payload->TryGetStringField(TEXT("out_dir"), OutDir) || OutDir.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("out_dir is required"));
    }
    if (!IsInsideMirrorRoot(OutDir / TEXT("key.json")))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_root"), TEXT("out_dir must be inside the registered mirror root (call transcode_root_set first)"));
    }
    const TArray<TSharedPtr<FJsonValue>>* Requests = nullptr;
    if (Payload->TryGetArrayField(TEXT("functions"), Requests) && Requests->Num() > 128)
    {
        return MakeOperationError(Operation, RequestId, TEXT("query_too_large"), TEXT("at most 128 function signatures per query"));
    }
    bool bDetailsOnly = false;
    Payload->TryGetBoolField(TEXT("details_only"), bDetailsOnly);
    const FString Key = SchemaKey();
    TArray<TPair<FString, TSharedPtr<FJsonObject>>> Files;
    TSharedPtr<FJsonObject> KeyJson = MakeShared<FJsonObject>();
    KeyJson->SetStringField(TEXT("key"), Key);
    KeyJson->SetStringField(TEXT("engine_version"), EngineVersionString());
    KeyJson->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
    KeyJson->SetObjectField(TEXT("environment"), SchemaEnvironment());
    Files.Emplace(TEXT("key.json"), KeyJson);
    if (!bDetailsOnly)
    {
    Files.Emplace(TEXT("classes.material_expression.json"), MaterialExpressionClasses());
    Files.Emplace(TEXT("classes.k2node.json"), BuildK2NodeSchema());
    Files.Emplace(TEXT("classes.asset.json"), ClassFamily([](UClass* Class)
    {
        const FString Name = Class->GetName();
        return Class == UMaterial::StaticClass() || Class == UMaterialFunction::StaticClass() || Class == UMaterialInstanceConstant::StaticClass()
            || Class == UBlueprint::StaticClass() || IsGenericAssetClass(Class) || Class->IsChildOf(AActor::StaticClass()) || Name == TEXT("NiagaraSystem") || Name == TEXT("NiagaraEmitter");
    }));
    Files.Emplace(TEXT("classes.component.json"), ClassFamily([](UClass* Class)
    {
        return Class->IsChildOf(UActorComponent::StaticClass());
    }));
    Files.Emplace(TEXT("classes.niagara_renderer.json"), ClassFamily([](UClass* Class)
    {
        return Class->GetName().StartsWith(TEXT("Niagara")) && Class->GetName().EndsWith(TEXT("RendererProperties"));
    }));
    Files.Emplace(TEXT("material_functions.json"), MaterialFunctionSignatures());
    Files.Emplace(TEXT("functions.index.json"), CallableFunctionIndex());
    Files.Emplace(TEXT("types.json"), CommonTypes());
    }
    const TSharedPtr<FJsonObject> Functions = RequestedFunctionSignatures(Payload);

    int32 Written = 0;
    for (const TPair<FString, TSharedPtr<FJsonObject>>& File : Files)
    {
        FString Error;
        if (!WriteJsonFile(OutDir / File.Key, File.Value, Error))
        {
            return MakeOperationError(Operation, RequestId, TEXT("write_failed"), Error);
        }
        ++Written;
    }
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("schema_key"), Key);
    Data->SetStringField(TEXT("out_dir"), OutDir);
    Data->SetNumberField(TEXT("files"), Written);
    Data->SetObjectField(TEXT("functions"), Functions);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
