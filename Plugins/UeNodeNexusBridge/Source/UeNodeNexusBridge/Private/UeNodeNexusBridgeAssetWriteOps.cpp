#include "UeNodeNexusBridgeOperations.h"

#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
static UObject* LoadRequiredAsset(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutResponse, const FString& Operation, const FString& RequestId)
{
    FString AssetPath;
    if (!Payload->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("invalid_request"), TEXT("asset_path is required")));
        return nullptr;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (Asset == nullptr)
    {
        OutResponse = MakeEnvelope(Operation, RequestId, false);
        OutResponse->SetObjectField(TEXT("error"), MakeError(TEXT("asset_not_found"), TEXT("Asset could not be loaded")));
        return nullptr;
    }

    return Asset;
}

static FString SeverityToString(EMessageSeverity::Type Severity)
{
    if (Severity == EMessageSeverity::Error)
    {
        return TEXT("error");
    }
    if (Severity == EMessageSeverity::Warning)
    {
        return TEXT("warning");
    }
    return TEXT("info");
}

static TArray<TSharedPtr<FJsonValue>> CompilerMessagesToDiagnostics(const FCompilerResultsLog& Results, const FString& AssetPath)
{
    TArray<TSharedPtr<FJsonValue>> Diagnostics;
    for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
        Diagnostic->SetStringField(TEXT("severity"), SeverityToString(Message->GetSeverity()));
        Diagnostic->SetStringField(TEXT("code"), TEXT("compile_message"));
        Diagnostic->SetStringField(TEXT("message"), Message->ToText().ToString());
        Diagnostic->SetStringField(TEXT("asset_path"), AssetPath);
        Diagnostic->SetStringField(TEXT("source"), TEXT("Unreal"));
        Diagnostic->SetStringField(TEXT("raw"), Message->ToText().ToString());
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
    return Diagnostics;
}

static TSharedPtr<FJsonObject> MakeCompileData(bool bRequested, bool bRan, bool bOk, int32 ErrorCount, int32 WarningCount)
{
    TSharedPtr<FJsonObject> Compile = MakeShared<FJsonObject>();
    Compile->SetBoolField(TEXT("requested"), bRequested);
    Compile->SetBoolField(TEXT("ran"), bRan);
    Compile->SetBoolField(TEXT("ok"), bOk);
    Compile->SetNumberField(TEXT("error_count"), ErrorCount);
    Compile->SetNumberField(TEXT("warning_count"), WarningCount);
    return Compile;
}

static void AddMaterialCompileDiagnostics(const TArray<FString>& CompileErrors, const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    for (const FString& CompileError : CompileErrors)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeDiagnostic(TEXT("error"), TEXT("material_compile_error"), CompileError, AssetPath, TEXT("Unreal.MaterialCompiler"));
        Diagnostic->SetStringField(TEXT("raw"), CompileError);
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
}

static TArray<FString> CollectMaterialCompileErrors(UMaterialInterface* MaterialInterface)
{
    TArray<FString> CompileErrors;
    if (MaterialInterface == nullptr)
    {
        return CompileErrors;
    }

    FMaterialResource* MaterialResource = MaterialInterface->GetMaterialResource(ERHIFeatureLevel::SM6);
    if (MaterialResource == nullptr)
    {
        return CompileErrors;
    }

    for (const FString& CompileError : MaterialResource->GetCompileErrors())
    {
        CompileErrors.AddUnique(CompileError);
    }
    return CompileErrors;
}

TSharedPtr<FJsonObject> HandleAssetCompile(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadRequiredAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }

    FString AssetPath;
    Payload->TryGetStringField(TEXT("asset_path"), AssetPath);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), AssetPath);
    Data->SetStringField(TEXT("asset_class"), Asset->GetClass()->GetPathName());

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        FCompilerResultsLog Results;
        Results.bSilentMode = true;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
        const bool bOk = Results.NumErrors == 0 && Blueprint->Status != BS_Error;
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, true, bOk, Results.NumErrors, Results.NumWarnings));
        Response->SetArrayField(TEXT("diagnostics"), CompilerMessagesToDiagnostics(Results, AssetPath));
        Response->SetBoolField(TEXT("ok"), bOk);
    }
    else if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        Material->ForceRecompileForRendering();
        Material->MarkPackageDirty();
        const TArray<FString> CompileErrors = CollectMaterialCompileErrors(Material);
        const bool bOk = CompileErrors.Num() == 0;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        AddMaterialCompileDiagnostics(CompileErrors, AssetPath, Diagnostics);
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, true, bOk, CompileErrors.Num(), 0));
        Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
        Response->SetBoolField(TEXT("ok"), bOk);
    }
    else if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
        UMaterialInterface* PreviewMaterial = Function->GetPreviewMaterial();
        const TArray<FString> CompileErrors = CollectMaterialCompileErrors(PreviewMaterial);
        const bool bOk = CompileErrors.Num() == 0;
        TArray<TSharedPtr<FJsonValue>> Diagnostics;
        AddMaterialCompileDiagnostics(CompileErrors, AssetPath, Diagnostics);
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, true, bOk, CompileErrors.Num(), 0));
        Response->SetArrayField(TEXT("diagnostics"), Diagnostics);
        Response->SetBoolField(TEXT("ok"), bOk);
    }
    else
    {
        Response->SetBoolField(TEXT("ok"), false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("unsupported_asset_class"), TEXT("Only Blueprint, Material, and MaterialFunction assets support compile")));
        Data->SetObjectField(TEXT("compile"), MakeCompileData(true, false, false, 0, 0));
    }

    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleAssetValidate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    return HandleAssetCompile(Operation, RequestId, Payload);
}

TSharedPtr<FJsonObject> HandleAssetSave(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> EarlyResponse;
    UObject* Asset = LoadRequiredAsset(Payload, EarlyResponse, Operation, RequestId);
    if (Asset == nullptr)
    {
        return EarlyResponse;
    }

    bool bOnlyIfDirty = true;
    Payload->TryGetBoolField(TEXT("only_if_dirty"), bOnlyIfDirty);

    UPackage* Package = Asset->GetOutermost();
    const bool bWasDirty = Package != nullptr && Package->IsDirty();
    const bool bSaved = Package != nullptr && UEditorLoadingAndSavingUtils::SavePackages({ Package }, bOnlyIfDirty);

    TSharedPtr<FJsonObject> DirtyState = MakeShared<FJsonObject>();
    DirtyState->SetBoolField(TEXT("was_dirty"), bWasDirty);
    DirtyState->SetBoolField(TEXT("package_dirty"), Package != nullptr && Package->IsDirty());
    DirtyState->SetBoolField(TEXT("saved"), bSaved);
    DirtyState->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    DirtyState->SetStringField(TEXT("filename"), Package ? FPackageName::LongPackageNameToFilename(Package->GetName()) : FString());

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Data->SetObjectField(TEXT("dirty_state"), DirtyState);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, bSaved);
    Response->SetObjectField(TEXT("data"), Data);
    if (!bSaved)
    {
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("save_failed"), TEXT("Package save failed or package is unavailable")));
    }
    return Response;
}
}
