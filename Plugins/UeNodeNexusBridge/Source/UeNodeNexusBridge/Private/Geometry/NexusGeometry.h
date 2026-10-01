#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "DynamicMesh/DynamicMesh3.h"

class UStaticMesh;

namespace UeNodeNexusBridge::Geometry
{
using FObject = TSharedPtr<FJsonObject>;
using FMesh = UE::Geometry::FDynamicMesh3;

struct FContext
{
    FMesh Mesh;
    UStaticMesh* Source = nullptr;
    FString SourceRevision;
    int32 MaxTriangles = 200000;
    double Deadline = 0;
    FObject Recipe;
    TArray<TSharedPtr<FJsonValue>> Steps;
};

FString String(const FObject& Object, const TCHAR* Key, const FString& Default = TEXT(""));
double Number(const FObject& Object, const TCHAR* Key, double Default);
bool Boolean(const FObject& Object, const TCHAR* Key, bool Default);
bool Vector(const FObject& Object, const TCHAR* Key, FVector3d& Value, bool Required = true);
FObject Child(const FObject& Object, const TCHAR* Key);
bool Fail(FString& Error, const FString& Message);
FString Revision(UStaticMesh* Asset);
bool LoadSource(const FObject& Recipe, FContext& Context, FString& Error);
bool ValidateRecipe(const FObject& Recipe, FString& Error);
bool CheckDestination(const FContext& Context, const FString& Path, FString& Error);
bool Lattice(FMesh& Mesh, const FObject& Op, FString& Error);
bool Noise(FMesh& Mesh, const FObject& Op, FString& Error);
bool Remesh(FContext& Context, const FObject& Op, FString& Error);
double Mask(const FMesh& Mesh, int32 Vertex, const FObject& Op);
FObject Quality(const FMesh& Mesh);
bool Validate(const FMesh& Mesh, int32 MaxTriangles, FString& Error);
bool CheckDeformation(const FMesh& Before, const FMesh& After, FString& Error);
bool Process(FContext& Context, FString& Error);
bool Publish(FContext& Context, const FString& Path, const FString& RecipeText, bool Save, FObject& Result, FString& Error);
FObject Inspect(UStaticMesh* Asset);
}

namespace UeNodeNexusBridge
{
TSharedPtr<FJsonObject> HandleMeshGeometryGet(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
TSharedPtr<FJsonObject> HandleMeshGeometryBuild(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload);
}
