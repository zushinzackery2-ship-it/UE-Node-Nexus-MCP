#include "UeNodeNexusBridgeOperations.h"

#include "GameFramework/Actor.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeLevelActorTransformError(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(Code, Message));
    return Response;
}

static AActor* ResolveActorForTransform(const TSharedPtr<FJsonObject>& Payload, TSharedPtr<FJsonObject>& OutError, const FString& Operation, const FString& RequestId)
{
    FString ActorPath;
    if (!Payload->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
    {
        OutError = MakeLevelActorTransformError(Operation, RequestId, TEXT("invalid_request"), TEXT("actor_path is required"));
        return nullptr;
    }

    AActor* Actor = Cast<AActor>(ResolveObjectByPath(ActorPath));
    if (Actor == nullptr)
    {
        OutError = MakeLevelActorTransformError(Operation, RequestId, TEXT("actor_not_found"), TEXT("Actor could not be resolved"));
        return nullptr;
    }
    return Actor;
}

TSharedPtr<FJsonObject> HandleLevelActorTransformGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    AActor* Actor = ResolveActorForTransform(Payload, Error, Operation, RequestId);
    if (Actor == nullptr)
    {
        return Error;
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetObjectField(TEXT("transform"), MakeTransformJson(Actor->GetActorTransform()));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
