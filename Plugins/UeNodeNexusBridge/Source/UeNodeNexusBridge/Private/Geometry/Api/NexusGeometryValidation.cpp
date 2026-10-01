#include "Geometry/NexusGeometry.h"

namespace UeNodeNexusBridge::Geometry
{
static bool Keys(const FObject& Object, const TArray<FString>& Allowed, FString& Error)
{
    if (!Object) return Fail(Error, TEXT("expected JSON object"));
    for (const auto& Pair : Object->Values)
    {
        if (!Allowed.Contains(Pair.Key)) return Fail(Error, TEXT("unknown geometry field: ") + Pair.Key);
        if (!Pair.Value || Pair.Value->Type == EJson::Null) return Fail(Error, TEXT("null geometry field: ") + Pair.Key);
    }
    return true;
}

static bool Booleans(const FObject& Object, FString& Error)
{
    for (const TCHAR* Key : {TEXT("lock_boundary"), TEXT("automatic"), TEXT("invert")})
    {
        bool Value;
        if (Object->HasField(Key) && !Object->TryGetBoolField(Key, Value)) return Fail(Error, FString(Key) + TEXT(" must be boolean"));
    }
    return true;
}

bool ValidateRecipe(const FObject& Recipe, FString& Error)
{
    if (!Keys(Recipe, {TEXT("source_asset"), TEXT("source_revision"), TEXT("grid"), TEXT("ops"), TEXT("max_triangles")}, Error)) return false;
    if (Recipe->HasField(TEXT("grid")) && !Keys(Child(Recipe, TEXT("grid")), {TEXT("size"), TEXT("cells")}, Error)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Ops = nullptr;
    if (!Recipe->TryGetArrayField(TEXT("ops"), Ops) || Ops->Num() > 32) return Fail(Error, TEXT("ops must contain at most 32 objects"));
    for (const auto& Value : *Ops)
    {
        const FObject* Op = nullptr;
        if (!Value || !Value->TryGetObject(Op)) return Fail(Error, TEXT("invalid geometry operation object"));
        const FString Type = String(*Op, TEXT("op"));
        TArray<FString> Allowed;
        if (Type == TEXT("lattice")) Allowed = {TEXT("op"), TEXT("dimensions"), TEXT("offsets"), TEXT("interpolation"), TEXT("mask"), TEXT("lock_boundary")};
        else if (Type == TEXT("noise")) Allowed = {TEXT("op"), TEXT("amplitude"), TEXT("scale"), TEXT("octaves"), TEXT("seed"), TEXT("persistence"), TEXT("sampling_offset"), TEXT("direction"), TEXT("mask"), TEXT("lock_boundary")};
        else if (Type == TEXT("remesh")) Allowed = {TEXT("op"), TEXT("target_edge_length"), TEXT("iterations"), TEXT("automatic"), TEXT("lock_boundary"), TEXT("region")};
        else return Fail(Error, TEXT("unknown geometry operation: ") + Type);
        if (!Keys(*Op, Allowed, Error) || !Booleans(*Op, Error)) return false;
        if ((*Op)->HasField(TEXT("mask")))
        {
            FObject Spec = Child(*Op, TEXT("mask"));
            if (!Keys(Spec, {TEXT("center"), TEXT("radius"), TEXT("invert")}, Error) || !Booleans(Spec, Error)) return false;
        }
        if ((*Op)->HasField(TEXT("region")) && !Keys(Child(*Op, TEXT("region")), {TEXT("min"), TEXT("max")}, Error)) return false;
        if (Type == TEXT("lattice"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Offsets = nullptr;
            if (!(*Op)->TryGetArrayField(TEXT("offsets"), Offsets)) return Fail(Error, TEXT("offsets must be an array"));
            for (const auto& Offset : *Offsets)
            {
                const FObject* Entry = nullptr;
                if (!Offset->TryGetObject(Entry) || !Keys(*Entry, {TEXT("index"), TEXT("delta")}, Error)) return false;
            }
        }
    }
    return true;
}
}
