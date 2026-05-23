#include "UeNodeNexusBridgeOperations.h"

#include "Components/ActorComponent.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeObjectHelpers.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge
{
static TSharedPtr<FJsonObject> MakeObjectMissingResponse(const FString& Operation, const FString& RequestId, const FString& Code, const FString& Message)
{
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, false);
    Response->SetObjectField(TEXT("error"), MakeError(Code, Message));
    return Response;
}

static void AddObjectIdentity(UObject* Object, TSharedPtr<FJsonObject> Json)
{
    Json->SetStringField(TEXT("object_path"), Object ? Object->GetPathName() : FString());
    Json->SetStringField(TEXT("object_name"), Object ? Object->GetName() : FString());
    Json->SetStringField(TEXT("class_path"), Object && Object->GetClass() ? Object->GetClass()->GetPathName() : FString());
}

static TSharedPtr<FJsonObject> ComponentToJson(UActorComponent* Component, bool bIncludeProperties)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    AddObjectIdentity(Component, Json);
    Json->SetStringField(TEXT("component_name"), Component ? Component->GetName() : FString());
    if (bIncludeProperties)
    {
        TSharedPtr<FJsonObject> PropPayload = MakeShared<FJsonObject>();
        PropPayload->SetStringField(TEXT("object_path"), Component ? Component->GetPathName() : FString());
        PropPayload->SetBoolField(TEXT("include_non_editable"), false);
        PropPayload->SetStringField(TEXT("format"), TEXT("compact"));
    }
    return Json;
}

static TSharedPtr<FJsonObject> ActorDetailsToJson(AActor* Actor, bool bIncludeComponents)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    AddObjectIdentity(Actor, Json);
    Json->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
    Json->SetStringField(TEXT("level_path"), Actor->GetLevel() ? Actor->GetLevel()->GetPathName() : FString());
    Json->SetObjectField(TEXT("transform"), MakeTransformJson(Actor->GetActorTransform()));

    if (bIncludeComponents)
    {
        TArray<TSharedPtr<FJsonValue>> Components;
        TInlineComponentArray<UActorComponent*> ActorComponents;
        Actor->GetComponents(ActorComponents);
        for (UActorComponent* Component : ActorComponents)
        {
            if (Component != nullptr)
            {
                Components.Add(MakeShared<FJsonValueObject>(ComponentToJson(Component, false)));
            }
        }
        Json->SetArrayField(TEXT("components"), Components);
    }
    return Json;
}

static void CollectPropertyNames(const TSharedPtr<FJsonObject>& Payload, TArray<FString>& OutPropertyNames)
{
    Payload->TryGetStringArrayField(TEXT("property_names"), OutPropertyNames);
    OutPropertyNames.RemoveAll([](const FString& Name)
    {
        return Name.IsEmpty();
    });
}

static bool PropertyNameMatches(FProperty* Property, const TArray<FString>& PropertyNames)
{
    if (PropertyNames.Num() == 0)
    {
        return true;
    }
    for (const FString& Name : PropertyNames)
    {
        if (Property->GetName().Equals(Name, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

TSharedPtr<FJsonObject> HandleLevelActorGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString ActorPath;
    if (!Payload->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
    {
        return MakeObjectMissingResponse(Operation, RequestId, TEXT("invalid_request"), TEXT("actor_path is required"));
    }

    AActor* Actor = Cast<AActor>(ResolveObjectByPath(ActorPath));
    if (Actor == nullptr)
    {
        return MakeObjectMissingResponse(Operation, RequestId, TEXT("actor_not_found"), TEXT("Actor could not be resolved"));
    }

    bool bIncludeComponents = false;
    Payload->TryGetBoolField(TEXT("include_components"), bIncludeComponents);

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), ActorDetailsToJson(Actor, bIncludeComponents));
    return Response;
}

TSharedPtr<FJsonObject> HandleObjectPropertiesGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    FString ObjectPath;
    if (!Payload->TryGetStringField(TEXT("object_path"), ObjectPath) || ObjectPath.IsEmpty())
    {
        return MakeObjectMissingResponse(Operation, RequestId, TEXT("invalid_request"), TEXT("object_path is required"));
    }

    UObject* Object = ResolveObjectByPath(ObjectPath);
    if (Object == nullptr)
    {
        return MakeObjectMissingResponse(Operation, RequestId, TEXT("object_not_found"), TEXT("Object could not be resolved"));
    }

    TArray<FString> PropertyNames;
    CollectPropertyNames(Payload, PropertyNames);
    bool bIncludeNonEditable = false;
    Payload->TryGetBoolField(TEXT("include_non_editable"), bIncludeNonEditable);
    FString Format = TEXT("compact");
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);

    TArray<TSharedPtr<FJsonValue>> Items;
    for (TFieldIterator<FProperty> It(Object->GetClass()); It; ++It)
    {
        FProperty* Property = *It;
        if (!ShouldExposeProperty(Property, bIncludeNonEditable) || !PropertyNameMatches(Property, PropertyNames))
        {
            continue;
        }
        if (bFull)
        {
            Items.Add(MakeShared<FJsonValueObject>(PropertyToJson(Object, Property, true)));
        }
        else
        {
            TArray<TSharedPtr<FJsonValue>> Row;
            Row.Add(MakeShared<FJsonValueString>(Property->GetName()));
            Row.Add(MakeShared<FJsonValueString>(Property->GetClass()->GetName()));
            Row.Add(PropertyValueToJson(Object, Property));
            Items.Add(MakeShared<FJsonValueArray>(Row));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    AddObjectIdentity(Object, Data);
    if (!bFull)
    {
        Data->SetStringField(TEXT("format"), TEXT("object_properties_compact"));
        Data->SetArrayField(TEXT("columns"), {
            MakeShared<FJsonValueString>(TEXT("name")),
            MakeShared<FJsonValueString>(TEXT("type")),
            MakeShared<FJsonValueString>(TEXT("value"))
        });
    }
    Data->SetArrayField(TEXT("items"), Items);
    Data->SetNumberField(TEXT("count"), Items.Num());

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}

}
