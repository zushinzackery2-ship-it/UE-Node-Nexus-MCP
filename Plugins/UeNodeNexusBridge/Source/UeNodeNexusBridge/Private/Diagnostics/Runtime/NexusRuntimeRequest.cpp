#include "NexusRuntimeDiagnostics.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
bool ValidateRequest(const TSharedPtr<FJsonObject>& Payload, FString& Error)
{
    if (!Payload)
    {
        Error = TEXT("diagnostics payload must be an object");
        return false;
    }
    for (const TCHAR* Field :
        { TEXT("asset_path"), TEXT("session_id") })
    {
        const auto Value = Payload->TryGetField(Field);
        if (Value && Value->Type != EJson::Null && (Value->Type != EJson::String || Value->AsString().IsEmpty()))
        {
            Error = FString(Field) + TEXT(" must be a nonempty string or null");
            return false;
        }
    }
    for (const TCHAR* Field :
        { TEXT("include_assets"), TEXT("include_history") })
    {
        const auto Value = Payload->TryGetField(Field);
        if (Value && Value->Type != EJson::Boolean)
        {
            Error = FString(Field) + TEXT(" must be a boolean");
            return false;
        }
    }
    const auto Severity = Payload->TryGetField(TEXT("severity"));
    if (Severity && (Severity->Type != EJson::String
        || (Severity->AsString() != TEXT("all") && Severity->AsString() != TEXT("info")
            && Severity->AsString() != TEXT("warning") && Severity->AsString() != TEXT("error"))))
    {
        Error = TEXT("severity must be all, info, warning or error");
        return false;
    }
    for (const TCHAR* Field :
        { TEXT("cursor"), TEXT("limit") })
    {
        const auto Value = Payload->TryGetField(Field);
        if (!Value)
        {
            continue;
        }
        const bool bLimit = FString(Field) == TEXT("limit");
        double Number = 0;
        if (Value->Type != EJson::Number || !Value->TryGetNumber(Number) || !FMath::IsFinite(Number)
            || Number < (bLimit ? 1 : 0) || Number > (bLimit ? 2000 : 9007199254740991.0)
            || FMath::FloorToDouble(Number) != Number)
        {
            Error = FString(Field) + (bLimit ? TEXT(" must be an integer from 1 to 2000")
                : TEXT(" must be a nonnegative safe integer"));
            return false;
        }
    }
    return true;
}
}
