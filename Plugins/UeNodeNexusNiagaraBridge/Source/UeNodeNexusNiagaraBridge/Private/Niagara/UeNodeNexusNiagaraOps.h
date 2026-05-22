#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleNiagaraSystemCreate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraSystemDuplicate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraTemplateDuplicate(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraSystemSummaryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraEmittersList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraUserParamsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraUserParamsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraMaterialsGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraMaterialsSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleNiagaraCompile(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
}
