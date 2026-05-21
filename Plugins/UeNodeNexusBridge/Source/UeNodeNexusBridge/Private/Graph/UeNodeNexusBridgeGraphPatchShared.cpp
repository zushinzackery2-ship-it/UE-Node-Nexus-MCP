#include "UeNodeNexusBridgeGraphPatchShared.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UeNodeNexusBridge
{
void AppendDiffItem(TSharedPtr<FJsonObject> Diff, const FString& Field, const TSharedPtr<FJsonObject>& Item)
{
    TArray<TSharedPtr<FJsonValue>> Items = Diff->GetArrayField(Field);
    Items.Add(MakeShared<FJsonValueObject>(Item));
    Diff->SetArrayField(Field, Items);
}

void AddGraphParamChange(TSharedPtr<FJsonObject> Diff, const FString& NodeId, const FString& Name, const FString& Before, const FString& After)
{
    TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
    Item->SetStringField(TEXT("node_id"), NodeId);
    Item->SetStringField(TEXT("name"), Name);
    Item->SetStringField(TEXT("before"), Before);
    Item->SetStringField(TEXT("after"), After);
    AppendDiffItem(Diff, TEXT("params_changed"), Item);
}

bool ReadGraphPosition(const TSharedPtr<FJsonObject>& Json, int32& OutX, int32& OutY)
{
    const TSharedPtr<FJsonObject>* Position = nullptr;
    const TSharedPtr<FJsonObject> Source = Json->TryGetObjectField(TEXT("position"), Position) && Position != nullptr ? *Position : Json;
    double X = 0.0;
    double Y = 0.0;
    if (!Source->TryGetNumberField(TEXT("x"), X) || !Source->TryGetNumberField(TEXT("y"), Y))
    {
        return false;
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}

bool ReadJsonScalarAsString(const TSharedPtr<FJsonObject>& Json, const FString& Field, FString& OutValue)
{
    if (Json->TryGetStringField(Field, OutValue))
    {
        return true;
    }
    double Number = 0.0;
    if (Json->TryGetNumberField(Field, Number))
    {
        OutValue = FString::SanitizeFloat(Number);
        return true;
    }
    bool bBool = false;
    if (Json->TryGetBoolField(Field, bBool))
    {
        OutValue = bBool ? TEXT("true") : TEXT("false");
        return true;
    }
    return false;
}
}
