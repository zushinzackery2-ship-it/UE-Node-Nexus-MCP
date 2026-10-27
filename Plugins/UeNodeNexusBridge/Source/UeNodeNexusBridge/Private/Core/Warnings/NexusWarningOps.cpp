#include "NexusWarningCapture.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UeNodeNexusBridge
{
namespace
{
bool ReadNames(const TSharedPtr<FJsonObject>& Payload, const TCHAR* Field, TArray<FName>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Payload.IsValid() || !Payload->TryGetArrayField(Field, Values) || Values == nullptr)
    {
        return true;
    }
    Out.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Name;
        if (!Value.IsValid() || !Value->TryGetString(Name) || Name.IsEmpty())
        {
            return false;
        }
        Out.Add(FName(*Name));
    }
    return true;
}
}

TSharedPtr<FJsonObject> HandleLogMessagesGet(const FString& Operation, const FString& RequestId,
                                             const TSharedPtr<FJsonObject>& Payload)
{
    double Since = 0.0;
    double Limit = 64.0;
    FString Severity;
    FString Match;
    TArray<FName> Categories;
    if (Payload.IsValid())
    {
        Payload->TryGetNumberField(TEXT("since"), Since);
        Payload->TryGetNumberField(TEXT("limit"), Limit);
        Payload->TryGetStringField(TEXT("severity"), Severity);
        Payload->TryGetStringField(TEXT("match"), Match);
    }
    Severity.ToLowerInline();
    const bool bKnownSeverity = Severity.IsEmpty() || Severity == TEXT("all")
        || Severity == TEXT("warning") || Severity == TEXT("error");
    if (!bKnownSeverity || Since < 0.0 || Limit < 1.0 || Limit > Warnings::FJournal::Capacity
        || !ReadNames(Payload, TEXT("categories"), Categories))
    {
        return MakeOperationError(Operation, RequestId, TEXT("invalid_request"),
                                  TEXT("severity must be warning, error or all; since must not be negative; "
                                       "limit must be between 1 and 256; categories must be non-empty strings"));
    }
    if (Severity == TEXT("all"))
    {
        Severity.Reset();
    }
    bool bDropped = false;
    const TArray<TSharedPtr<FJsonValue>> Items = Warnings::Journal().Window(
        static_cast<int64>(Since), bDropped, static_cast<int32>(Limit), Severity, Categories, Match);
    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetNumberField(TEXT("cursor"), static_cast<double>(Warnings::Journal().Cursor()));
    Data->SetBoolField(TEXT("dropped"), bDropped);
    Data->SetArrayField(TEXT("items"), Items);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
