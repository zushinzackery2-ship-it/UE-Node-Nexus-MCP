#include "Geometry/NexusGeometry.h"

#include "Engine/StaticMesh.h"
#include "Generators/RectangleMeshGenerator.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "Misc/SecureHash.h"
#include "Serialization/MemoryWriter.h"

namespace UeNodeNexusBridge::Geometry
{
FString String(const FObject& Object, const TCHAR* Key, const FString& Default)
{
    FString Value = Default;
    if (Object) Object->TryGetStringField(Key, Value);
    return Value;
}

double Number(const FObject& Object, const TCHAR* Key, double Default)
{
    double Value = Default;
    if (Object && Object->HasField(Key) && !Object->TryGetNumberField(Key, Value)) return NAN;
    return Value;
}

bool Boolean(const FObject& Object, const TCHAR* Key, bool Default)
{
    bool Value = Default;
    if (Object) Object->TryGetBoolField(Key, Value);
    return Value;
}

FObject Child(const FObject& Object, const TCHAR* Key)
{
    const FObject* Value = nullptr;
    return Object && Object->TryGetObjectField(Key, Value) ? *Value : nullptr;
}

bool Vector(const FObject& Object, const TCHAR* Key, FVector3d& Value, bool Required)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object || !Object->HasField(Key)) return !Required;
    if (!Object->TryGetArrayField(Key, Values) || Values->Num() != 3) return false;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        double NumberValue;
        if (!(*Values)[Index]->TryGetNumber(NumberValue) || !FMath::IsFinite(NumberValue)) return false;
        Value[Index] = NumberValue;
    }
    return true;
}

bool Fail(FString& Error, const FString& Message)
{
    Error = Message;
    return false;
}

FString Revision(UStaticMesh* Asset)
{
    FMeshDescription* Description = Asset ? Asset->GetMeshDescription(0) : nullptr;
    if (!Description) return TEXT("");
    TArray<uint8> Bytes;
    FMemoryWriter Writer(Bytes);
    Description->Serialize(Writer);
    for (const FStaticMaterial& Material : Asset->GetStaticMaterials())
    {
        FString Path = GetPathNameSafe(Material.MaterialInterface);
        Writer << Path;
    }
    FMD5 Hash;
    Hash.Update(Bytes.GetData(), Bytes.Num());
    uint8 Digest[16];
    Hash.Final(Digest);
    return BytesToHex(Digest, 16);
}

static bool Grid(const FObject& GridSpec, FContext& Context, FString& Error)
{
    FVector3d Size(1000, 1000, 0), Cells(32, 32, 0);
    if (!Vector(GridSpec, TEXT("size"), Size) || !Vector(GridSpec, TEXT("cells"), Cells))
        return Fail(Error, TEXT("grid size/cells require [x,y,0]"));
    if (Size.X <= 0 || Size.Y <= 0 || Size.GetAbsMax() > 1e7 || Size.Z != 0 || Cells.Z != 0)
        return Fail(Error, TEXT("grid size must be positive XY, at most 1e7 cm, Z=0"));
    if (Cells.X < 1 || Cells.Y < 1 || Cells.X > 512 || Cells.Y > 512 ||
        Cells.X != FMath::FloorToDouble(Cells.X) || Cells.Y != FMath::FloorToDouble(Cells.Y) ||
        Cells.X * Cells.Y * 2 > Context.MaxTriangles)
        return Fail(Error, TEXT("grid cells require integer XY 1..512 within max_triangles"));
    // Use UE's left-handed winding and matching normal/UV overlays.
    UE::Geometry::FRectangleMeshGenerator Generator;
    Generator.Width = Size.X;
    Generator.Height = Size.Y;
    Generator.WidthVertexCount = static_cast<int32>(Cells.X) + 1;
    Generator.HeightVertexCount = static_cast<int32>(Cells.Y) + 1;
    Generator.bSinglePolyGroup = true;
    Generator.Generate();
    return Context.Mesh.Copy(&Generator) || Fail(Error, TEXT("grid generation failed"));
}

bool LoadSource(const FObject& Recipe, FContext& Context, FString& Error)
{
    if (!Recipe) return Fail(Error, TEXT("recipe must be an object"));
    const FString Source = String(Recipe, TEXT("source_asset"));
    const FObject GridSpec = Child(Recipe, TEXT("grid"));
    if (Source.IsEmpty() == !GridSpec.IsValid()) return Fail(Error, TEXT("choose exactly one source_asset or grid"));
    const double Budget = Number(Recipe, TEXT("max_triangles"), 200000);
    if (!FMath::IsFinite(Budget) || Budget < 2 || Budget > 500000 || Budget != FMath::FloorToDouble(Budget))
        return Fail(Error, TEXT("max_triangles must be integer 2..500000"));
    Context.MaxTriangles = static_cast<int32>(Budget);
    Context.Recipe = Recipe;
    if (GridSpec) return Grid(GridSpec, Context, Error);
    Context.Source = LoadObject<UStaticMesh>(nullptr, *Source);
    if (!Context.Source || !Context.Source->GetMeshDescription(0)) return Fail(Error, TEXT("source LOD0 MeshDescription unavailable"));
    Context.SourceRevision = Revision(Context.Source);
    if (String(Recipe, TEXT("source_revision")) != Context.SourceRevision)
        return Fail(Error, TEXT("source_conflict: use revision from mesh_geometry_get"));
    if (Context.Source->GetMeshDescription(0)->Triangles().Num() > Context.MaxTriangles)
        return Fail(Error, TEXT("source exceeds max_triangles"));
    FMeshDescriptionToDynamicMesh Converter;
    Converter.Convert(Context.Source->GetMeshDescription(0), Context.Mesh);
    return true;
}
}
