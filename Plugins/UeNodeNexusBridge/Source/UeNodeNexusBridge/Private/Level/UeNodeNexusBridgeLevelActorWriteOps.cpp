#include "UeNodeNexusBridgeOperations.h"

#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
namespace
{
bool ReadVectorField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, FVector& OutValue)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Payload->TryGetObjectField(Field, Object) || Object == nullptr || !Object->IsValid())
    {
        return false;
    }
    double X = 0.0, Y = 0.0, Z = 0.0;
    (*Object)->TryGetNumberField(TEXT("x"), X);
    (*Object)->TryGetNumberField(TEXT("y"), Y);
    (*Object)->TryGetNumberField(TEXT("z"), Z);
    OutValue = FVector(X, Y, Z);
    return true;
}

bool ReadRotatorField(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, FRotator& OutValue)
{
    const TSharedPtr<FJsonObject>* Object = nullptr;
    if (!Payload->TryGetObjectField(Field, Object) || Object == nullptr || !Object->IsValid())
    {
        return false;
    }
    double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
    (*Object)->TryGetNumberField(TEXT("pitch"), Pitch);
    (*Object)->TryGetNumberField(TEXT("yaw"), Yaw);
    (*Object)->TryGetNumberField(TEXT("roll"), Roll);
    OutValue = FRotator(Pitch, Yaw, Roll);
    return true;
}

UClass* ResolveActorClass(const FString& ClassPath)
{
    if (UClass* Direct = LoadClass<AActor>(nullptr, *ClassPath))
    {
        return Direct;
    }
    if (!ClassPath.Contains(TEXT("/")))
    {
        if (UClass* EngineClass = LoadClass<AActor>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassPath)))
        {
            return EngineClass;
        }
    }
    if (UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ClassPath))
    {
        UClass* Generated = Blueprint->GeneratedClass;
        if (Generated != nullptr && Generated->IsChildOf(AActor::StaticClass()))
        {
            return Generated;
        }
    }
    return nullptr;
}

UEditorActorSubsystem* GetActorSubsystemOrError(const FString& Operation, const FString& RequestId, TSharedPtr<FJsonObject>& OutError)
{
    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    if (ActorSubsystem == nullptr)
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("editor_actor_subsystem_unavailable"), TEXT("UEditorActorSubsystem is not available"));
    }
    return ActorSubsystem;
}

AActor* ResolveActorOrError(const TSharedPtr<FJsonObject>& Payload, const FString& Operation, const FString& RequestId, TSharedPtr<FJsonObject>& OutError)
{
    FString ActorPath;
    if (!Payload->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("actor_path is required"));
        return nullptr;
    }
    AActor* Actor = Cast<AActor>(ResolveObjectByPath(ActorPath));
    if (Actor == nullptr)
    {
        OutError = MakeOperationError(Operation, RequestId, TEXT("actor_not_found"), TEXT("Actor could not be resolved"));
    }
    return Actor;
}

TSharedPtr<FJsonObject> MakeActorWriteData(AActor* Actor, bool bDryRun, bool bApplied)
{
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (Actor != nullptr)
    {
        Data->SetStringField(TEXT("actor_path"), Actor->GetPathName());
        Data->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
        Data->SetStringField(TEXT("class_path"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString());
    }
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetBoolField(TEXT("applied"), bApplied);
    Data->SetBoolField(TEXT("changed"), bApplied);
    return Data;
}
}

TSharedPtr<FJsonObject> HandleLevelActorSpawn(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    UEditorActorSubsystem* ActorSubsystem = GetActorSubsystemOrError(Operation, RequestId, Error);
    if (ActorSubsystem == nullptr)
    {
        return Error;
    }

    FString ClassPath;
    if (!Payload->TryGetStringField(TEXT("class_path"), ClassPath) || ClassPath.IsEmpty())
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("class_path is required"));
    }
    UClass* ActorClass = ResolveActorClass(ClassPath);
    if (ActorClass == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("class_not_found"), TEXT("class_path did not resolve to an AActor subclass"));
    }

    FVector Location = FVector::ZeroVector;
    ReadVectorField(Payload, TEXT("location"), Location);
    FRotator Rotation = FRotator::ZeroRotator;
    ReadRotatorField(Payload, TEXT("rotation"), Rotation);
    FVector Scale = FVector::OneVector;
    const bool bHasScale = ReadVectorField(Payload, TEXT("scale"), Scale);
    FString Label;
    Payload->TryGetStringField(TEXT("label"), Label);

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    if (bDryRun)
    {
        TSharedPtr<FJsonObject> Data = MakeActorWriteData(nullptr, true, false);
        Data->SetStringField(TEXT("class_path"), ActorClass->GetPathName());
        Data->SetObjectField(TEXT("location"), MakeVectorJson(Location));
        Data->SetObjectField(TEXT("rotation"), MakeRotatorJson(Rotation));
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
        Response->SetObjectField(TEXT("data"), Data);
        return Response;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    ULevel* Level = World != nullptr ? World->GetCurrentLevel() : nullptr;
    if (Level == nullptr || World->WorldType != EWorldType::Editor)
    {
        return MakeOperationError(Operation, RequestId, TEXT("editor_world_unavailable"), TEXT("An editable level is required to spawn an actor"));
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.OverrideLevel = Level;
    SpawnParameters.ObjectFlags = RF_Transactional;
    SpawnParameters.bCreateActorPackage = Level->IsUsingExternalActors();
    SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Level->Modify();
    AActor* Actor = World->SpawnActor<AActor>(ActorClass, FTransform(Rotation, Location), SpawnParameters);
    if (Actor == nullptr)
    {
        return MakeOperationError(Operation, RequestId, TEXT("spawn_failed"), TEXT("Editor refused to spawn the actor"));
    }
    if (bHasScale)
    {
        Actor->SetActorScale3D(Scale);
    }
    if (!Label.IsEmpty())
    {
        Actor->SetActorLabel(Label);
    }
    Actor->MarkPackageDirty();

    TSharedPtr<FJsonObject> Data = MakeActorWriteData(Actor, false, true);
    Data->SetObjectField(TEXT("transform"), MakeTransformJson(Actor->GetActorTransform()));
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleLevelActorDelete(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    UEditorActorSubsystem* ActorSubsystem = GetActorSubsystemOrError(Operation, RequestId, Error);
    if (ActorSubsystem == nullptr)
    {
        return Error;
    }
    AActor* Actor = ResolveActorOrError(Payload, Operation, RequestId, Error);
    if (Actor == nullptr)
    {
        return Error;
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    TSharedPtr<FJsonObject> Data = MakeActorWriteData(Actor, bDryRun, false);
    if (!bDryRun)
    {
        if (!ActorSubsystem->DestroyActor(Actor))
        {
            return MakeOperationError(Operation, RequestId, TEXT("delete_failed"), TEXT("Editor refused to destroy the actor"));
        }
        Data->SetBoolField(TEXT("applied"), true);
        Data->SetBoolField(TEXT("changed"), true);
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

TSharedPtr<FJsonObject> HandleLevelActorTransformSet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    TSharedPtr<FJsonObject> Error;
    AActor* Actor = ResolveActorOrError(Payload, Operation, RequestId, Error);
    if (Actor == nullptr)
    {
        return Error;
    }

    FVector Location;
    const bool bHasLocation = ReadVectorField(Payload, TEXT("location"), Location);
    FRotator Rotation;
    const bool bHasRotation = ReadRotatorField(Payload, TEXT("rotation"), Rotation);
    FVector Scale;
    const bool bHasScale = ReadVectorField(Payload, TEXT("scale"), Scale);
    if (!bHasLocation && !bHasRotation && !bHasScale)
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"), TEXT("provide at least one of location / rotation / scale"));
    }

    bool bDryRun = true;
    Payload->TryGetBoolField(TEXT("dry_run"), bDryRun);

    TSharedPtr<FJsonObject> Data = MakeActorWriteData(Actor, bDryRun, !bDryRun);
    Data->SetObjectField(TEXT("transform_before"), MakeTransformJson(Actor->GetActorTransform()));
    if (!bDryRun)
    {
        Actor->Modify();
        if (bHasLocation)
        {
            Actor->SetActorLocation(Location);
        }
        if (bHasRotation)
        {
            Actor->SetActorRotation(Rotation);
        }
        if (bHasScale)
        {
            Actor->SetActorScale3D(Scale);
        }
        Actor->MarkPackageDirty();
    }
    Data->SetObjectField(TEXT("transform_after"), MakeTransformJson(Actor->GetActorTransform()));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
