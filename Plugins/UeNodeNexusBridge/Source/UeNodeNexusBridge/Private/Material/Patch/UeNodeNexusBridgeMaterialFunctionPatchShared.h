#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
class UMaterialExpression;
class UMaterialFunction;

namespace UeNodeNexusBridge
{
struct FMaterialFunctionPatchContext;

TSharedPtr<FJsonObject> MakeFunctionPatchLinkJson(const FString& FromNodeId, const FString& FromPinId, const FString& ToNodeId, const FString& ToPinId);
void AddFunctionPatchDiagnostic(TArray<TSharedPtr<FJsonValue>>& Diagnostics, const FString& Code, const FString& Message, UMaterialFunction* Function);
void CopyFunctionPatchOptionalStringField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName);
void CopyFunctionPatchOptionalField(const TSharedPtr<FJsonObject>& Source, const FString& SourceName, const TSharedPtr<FJsonObject>& Target, const FString& TargetName);
FString ReadFunctionPatchNodeRef(const TSharedPtr<FJsonObject>& Op, const FString& CanonicalField, const FString& AliasField);
UMaterialExpression* ResolveFunctionPatchNode(UMaterialFunction* Function, const FString& NodeId, const FMaterialFunctionPatchContext& Context);
bool SplitMaterialEndpointShorthand(const FString& Endpoint, FString& OutNodeId, FString& OutPinId);
bool ApplyMaterialFunctionPatchOperation(UMaterialFunction* Function, const TSharedPtr<FJsonObject>& Op, bool bDryRun, TSharedPtr<FJsonObject> Diff, TArray<TSharedPtr<FJsonValue>>& Diagnostics, FMaterialFunctionPatchContext& Context);
}
