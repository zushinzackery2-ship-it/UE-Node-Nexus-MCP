#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UeNodeNexusBridgeJson.h"

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleLevelCurrentGet(const FString& Operation, const FString& RequestId)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (World == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("no_editor_world"), TEXT("Editor world is not available")));
        return Response;
    }

    UPackage* Package = World->GetOutermost();
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("world_name"), World->GetName());
    Data->SetStringField(TEXT("world_path"), World->GetPathName());
    Data->SetStringField(TEXT("package_name"), Package ? Package->GetName() : FString());
    Data->SetBoolField(TEXT("is_dirty"), Package ? Package->IsDirty() : false);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

static bool ActorClassMatches(AActor* Actor, const TArray<FString>& ClassNames)
{
    if (ClassNames.Num() == 0)
    {
        return true;
    }

    const FString ClassPath = Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString();
    for (const FString& ClassName : ClassNames)
    {
        if (ClassPath.Equals(ClassName, ESearchCase::IgnoreCase) || ClassPath.EndsWith(TEXT(".") + ClassName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }

    return false;
}

static TSharedPtr<FJsonObject> ActorToJson(AActor* Actor, bool bIncludeComponents)
{
    TSharedPtr<FJsonObject> ActorJson = MakeShared<FJsonObject>();
    ActorJson->SetStringField(TEXT("actor_path"), Actor->GetPathName());
    ActorJson->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    ActorJson->SetStringField(TEXT("class_path"), Actor->GetClass() ? Actor->GetClass()->GetPathName() : FString());
    ActorJson->SetStringField(TEXT("level_path"), Actor->GetLevel() ? Actor->GetLevel()->GetPathName() : FString());

    const FVector Location = Actor->GetActorLocation();
    TSharedPtr<FJsonObject> LocationJson = MakeShared<FJsonObject>();
    LocationJson->SetNumberField(TEXT("x"), Location.X);
    LocationJson->SetNumberField(TEXT("y"), Location.Y);
    LocationJson->SetNumberField(TEXT("z"), Location.Z);
    ActorJson->SetObjectField(TEXT("location"), LocationJson);

    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> Components;
        TInlineComponentArray<UActorComponent*> ActorComponents;
        Actor->GetComponents(ActorComponents);
        for (UActorComponent* Component : ActorComponents)
        {
            if (Component == nullptr)
            {
                continue;
            }

            TSharedPtr<FJsonObject> ComponentJson = MakeShared<FJsonObject>();
            ComponentJson->SetStringField(TEXT("component_path"), Component->GetPathName());
            ComponentJson->SetStringField(TEXT("component_name"), Component->GetName());
            ComponentJson->SetStringField(TEXT("class_path"), Component->GetClass() ? Component->GetClass()->GetPathName() : FString());
            Components.Add(MakeShared<FJsonValueObject>(ComponentJson));
        }
        ActorJson->SetArrayField(TEXT("components"), Components);
    }

    return ActorJson;
}

static TSharedPtr<FJsonValue> ActorToRow(AActor* Actor)
{
    const FVector Location = Actor->GetActorLocation();
    TArray<TSharedPtr<FJsonValue>> Row;
    Row.Add(MakeShared<FJsonValueString>(Actor->GetPathName()));
    Row.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
    Row.Add(MakeShared<FJsonValueString>(Actor->GetClass() ? Actor->GetClass()->GetName() : FString()));
    Row.Add(MakeShared<FJsonValueNumber>(Location.X));
    Row.Add(MakeShared<FJsonValueNumber>(Location.Y));
    Row.Add(MakeShared<FJsonValueNumber>(Location.Z));
    return MakeShared<FJsonValueArray>(Row);
}

TSharedPtr<FJsonObject> HandleLevelActorsList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
    if (ActorSubsystem == nullptr)
    {
        TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
        Response->SetObjectField(TEXT("error"), MakeError(TEXT("editor_actor_subsystem_unavailable"), TEXT("UEditorActorSubsystem is not available")));
        return Response;
    }

    TArray<FString> ClassNames;
    Payload->TryGetStringArrayField(TEXT("class_names"), ClassNames);

    bool bIncludeComponents = false;
    Payload->TryGetBoolField(TEXT("include_components"), bIncludeComponents);
    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bCompact = !Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    const int32 Offset = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 200, 2000);
    const TArray<AActor*> Actors = ActorSubsystem->GetAllLevelActors();

    TArray<TSharedPtr<FJsonValue>> Items;
    int32 MatchedIndex = 0;
    bool bHasMore = false;

    for (AActor* Actor : Actors)
    {
        if (Actor == nullptr || Actor->IsPendingKillPending() || !ActorClassMatches(Actor, ClassNames))
        {
            continue;
        }
        if (MatchedIndex++ < Offset)
        {
            continue;
        }
        if (Items.Num() >= Limit)
        {
            bHasMore = true;
            break;
        }
        if (bCompact)
        {
            Items.Add(ActorToRow(Actor));
        }
        else
        {
            Items.Add(MakeShared<FJsonValueObject>(ActorToJson(Actor, bIncludeComponents)));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    if (bCompact)
    {
        Data->SetStringField(TEXT("format"), TEXT("level_actors_compact"));
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("actor_path")),
            MakeShared<FJsonValueString>(TEXT("label")),
            MakeShared<FJsonValueString>(TEXT("class")),
            MakeShared<FJsonValueString>(TEXT("x")),
            MakeShared<FJsonValueString>(TEXT("y")),
            MakeShared<FJsonValueString>(TEXT("z"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());
    Data->SetBoolField(TEXT("has_more"), bHasMore);
    if (bHasMore)
    {
        Data->SetStringField(TEXT("next_cursor"), FString::FromInt(Offset + Items.Num()));
    }

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
