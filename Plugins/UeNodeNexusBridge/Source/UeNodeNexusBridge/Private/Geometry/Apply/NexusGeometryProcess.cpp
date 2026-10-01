#include "Geometry/NexusGeometry.h"

namespace UeNodeNexusBridge::Geometry
{
bool Process(FContext& Context, FString& Error)
{
    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Context.Recipe->TryGetArrayField(TEXT("ops"), Operations) || Operations->Num() > 32)
        return Fail(Error, TEXT("recipe ops must be an array of at most 32 operations"));
    if (!Validate(Context.Mesh, Context.MaxTriangles, Error)) return false;
    for (const auto& Value : *Operations)
    {
        const FObject* Op = nullptr;
        if (!Value->TryGetObject(Op)) return Fail(Error, TEXT("geometry op must be an object"));
        const FString Type = String(*Op, TEXT("op"));
        const double Started = FPlatformTime::Seconds();
        if (Started > Context.Deadline) return Fail(Error, TEXT("geometry compute time budget exceeded"));
        bool Success = false;
        const FMesh Before(Context.Mesh);
        if (Type == TEXT("lattice")) Success = Lattice(Context.Mesh, *Op, Error);
        else if (Type == TEXT("noise")) Success = Noise(Context.Mesh, *Op, Error);
        else if (Type == TEXT("remesh")) Success = Remesh(Context, *Op, Error);
        else return Fail(Error, TEXT("unknown geometry operation: ") + Type);
        if (Success && Type != TEXT("remesh") && !CheckDeformation(Before, Context.Mesh, Error)) return false;
        if (!Success || !Validate(Context.Mesh, Context.MaxTriangles, Error)) return false;
        FObject Step = Quality(Context.Mesh);
        Step->SetStringField(TEXT("op"), Type);
        Step->SetNumberField(TEXT("seconds"), FPlatformTime::Seconds() - Started);
        Context.Steps.Add(MakeShared<FJsonValueObject>(Step));
        UE_LOG(LogTemp, Display, TEXT("[NexusGeometry] %s triangles=%d seconds=%.3f"),
            *Type, Context.Mesh.TriangleCount(), FPlatformTime::Seconds() - Started);
    }
    if (FPlatformTime::Seconds() > Context.Deadline) return Fail(Error, TEXT("geometry compute time budget exceeded"));
    return true;
}
}
