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

static bool TryReadNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* LowerName, const TCHAR* UpperName, double& Value)
{
    return Json.IsValid() && (Json->TryGetNumberField(LowerName, Value) || Json->TryGetNumberField(UpperName, Value));
}

static bool TryReadVector(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, const FVector& CurrentValue, FVector& OutValue)
{
    const TSharedPtr<FJsonObject>* Json = nullptr;
    if (!Payload->TryGetObjectField(FieldName, Json) || Json == nullptr)
    {
        OutValue = CurrentValue;
        return false;
    }

    double X = CurrentValue.X;
    double Y = CurrentValue.Y;
    double Z = CurrentValue.Z;
    TryReadNumber(*Json, TEXT("x"), TEXT("X"), X);
    TryReadNumber(*Json, TEXT("y"), TEXT("Y"), Y);
    TryReadNumber(*Json, TEXT("z"), TEXT("Z"), Z);
    OutValue = FVector(X, Y, Z);
    return true;
}

static bool TryReadRotator(const TSharedPtr<FJsonObject>& Payload, const TCHAR* FieldName, const FRotator& CurrentValue, FRotator& OutValue)
{
    const TSharedPtr<FJsonObject>* Json = nullptr;
    if (!Payload->TryGetObjectField(FieldName, Json) || Json == nullptr)
    {
        OutValue = CurrentValue;
        return false;
    }

    double Pitch = CurrentValue.Pitch;
    double Yaw = CurrentValue.Yaw;
    double Roll = CurrentValue.Roll;
    TryReadNumber(*Json, TEXT("pitch"), TEXT("Pitch"), Pitch);
    TryReadNumber(*Json, TEXT("yaw"), TEXT("Yaw"), Yaw);
    TryReadNumber(*Json, TEXT("roll"), TEXT("Roll"), Roll);
    OutValue = FRotator(Pitch, Yaw, Roll);
    return true;
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

TSharedPtr<FJsonObject> HandleLevelActorTransformSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    AActor* Actor = ResolveActorForTransform(Payload, Error, Operation, RequestId);
    if (Actor == nullptr)
    {
        return Error;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);
    bool bMarkDirty = true;
    Payload->TryGetBoolField(TEXT("mark_dirty"), bMarkDirty);

    const FTransform OldTransform = Actor->GetActorTransform();
    FVector NewLocation;
    FVector NewScale;
    FRotator NewRotation;
    const bool bHasLocation = TryReadVector(Payload, TEXT("location"), OldTransform.GetLocation(), NewLocation);
    const bool bHasScale = TryReadVector(Payload, TEXT("scale"), OldTransform.GetScale3D(), NewScale);
    const bool bHasRotation = TryReadRotator(Payload, TEXT("rotation"), OldTransform.Rotator(), NewRotation);
    const bool bHasAnyField = bHasLocation || bHasRotation || bHasScale;
    const FTransform NewTransform(NewRotation, NewLocation, NewScale);
    const bool bChanged = !NewTransform.Equals(OldTransform);

    if (!bDryRun && bHasAnyField)
    {
        if (bMarkDirty)
        {
            Actor->Modify();
        }
        Actor->SetActorTransform(NewTransform, false, nullptr, ETeleportType::TeleportPhysics);
        if (bMarkDirty)
        {
            Actor->MarkPackageDirty();
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("mark_dirty"), bMarkDirty);
    Data->SetBoolField(TEXT("applied"), !bDryRun && bHasAnyField);
    Data->SetBoolField(TEXT("changed"), bChanged);
    Data->SetObjectField(TEXT("old_transform"), MakeTransformJson(OldTransform));
    Data->SetObjectField(TEXT("new_transform"), MakeTransformJson(NewTransform));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
