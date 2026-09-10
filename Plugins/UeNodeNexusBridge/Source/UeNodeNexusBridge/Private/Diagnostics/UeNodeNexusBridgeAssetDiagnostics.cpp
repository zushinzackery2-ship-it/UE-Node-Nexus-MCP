#include "UeNodeNexusBridgeDiagnostics.h"
#include "Compilation/UeNodeNexusBridgeCompilation.h"

#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "MaterialEditingLibrary.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "RHI.h"
#include "Misc/UObjectToken.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
namespace
{
FString SeverityToString(EMessageSeverity::Type Severity)
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
}

TArray<FString> CollectMaterialCompileErrors(UMaterialInterface* MaterialInterface)
{
    TArray<FString> CompileErrors;
    if (MaterialInterface == nullptr)
    {
        return CompileErrors;
    }

    FMaterialResource* MaterialResource = MaterialInterface->GetMaterialResource(GMaxRHIFeatureLevel);
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

void AddMaterialCompileDiagnostics(const TArray<FString>& CompileErrors, const FString& AssetPath, TArray<TSharedPtr<FJsonValue>>& Diagnostics)
{
    for (const FString& CompileError : CompileErrors)
    {
        TSharedPtr<FJsonObject> Diagnostic = MakeDiagnostic(TEXT("error"), TEXT("material_compile_error"), CompileError, AssetPath, TEXT("Unreal.MaterialCompiler"));
        Diagnostic->SetStringField(TEXT("raw"), CompileError);
        Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
    }
}

FBridgeAssetCompileDiagnostics CollectAssetCompileDiagnostics(UObject* Asset, const FString& AssetPath, bool bMarkMaterialDirty)
{
    FBridgeAssetCompileDiagnostics Result;
    if (Asset == nullptr)
    {
        return Result;
    }

    Result.AssetClass = Asset->GetClass()->GetPathName();

    if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
    {
        Result.bSupported = true;
        Result.bRan = true;

        FCompilerResultsLog Results;
        Results.bSilentMode = true;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
        Result.bOk = Results.NumErrors == 0 && Blueprint->Status != BS_Error;
        Result.ErrorCount = Results.NumErrors;
        Result.WarningCount = Results.NumWarnings;
        Result.State = Result.bOk ? TEXT("ready") : TEXT("failed");

        for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
        {
            TSharedPtr<FJsonObject> Diagnostic = MakeDiagnostic(SeverityToString(Message->GetSeverity()), TEXT("compile_message"), Message->ToText().ToString(), AssetPath, TEXT("Unreal"));
            Diagnostic->SetStringField(TEXT("raw"), Message->ToText().ToString());
            // Node references let the text mirror map compiler messages back to file:line.
            for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
            {
                if (Token->GetType() != EMessageToken::Object)
                {
                    continue;
                }
                const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
                if (const UEdGraphNode* Node = Cast<UEdGraphNode>(ObjectToken->GetObject().Get()))
                {
                    Diagnostic->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
                    break;
                }
            }
            Result.Diagnostics.Add(MakeShared<FJsonValueObject>(Diagnostic));
        }
        return Result;
    }

    if (UMaterial* Material = Cast<UMaterial>(Asset))
    {
        Result.bSupported = true;
        Result.bRan = true;

        Material->ForceRecompileForRendering();
        if (bMarkMaterialDirty)
        {
            Material->MarkPackageDirty();
        }

        return MaterialResourceStatus(Material, true);
    }

    if (UMaterialFunction* Function = Cast<UMaterialFunction>(Asset))
    {
        Result.bSupported = true;
        Result.bRan = true;

        UMaterialEditingLibrary::UpdateMaterialFunction(Function, nullptr);
        UMaterialInterface* PreviewMaterial = Function->GetPreviewMaterial();
        return MaterialResourceStatus(PreviewMaterial, true);
    }

    if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset))
    {
        FBridgeAssetCompileDiagnostics Parent = MaterialResourceStatus(Instance->Parent, true);
        if (!Parent.bOk)
        {
            return Parent;
        }
        UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
        return MaterialResourceStatus(Instance, true);
    }

    return Result;
}
}
