#include "Geometry/NexusGeometry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMeshToMeshDescription.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "StaticMeshAttributes.h"
#include "UeNodeNexusBridgeTranscodeApi.h"
#include "UeNodeNexusCollaboration.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace UeNodeNexusBridge::Geometry
{
static constexpr const TCHAR* RecipeKey = TEXT("NexusGeometryRecipe");
static constexpr const TCHAR* DigestKey = TEXT("NexusGeometryDigest");
static constexpr const TCHAR* RevisionKey = TEXT("NexusGeometryRevision");

bool CheckDestination(const FContext& Context, const FString& Path, FString& Error)
{
    const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!Existing) return true;
    UStaticMesh* Asset = Cast<UStaticMesh>(Existing);
    UMetaData* Metadata = Existing->GetPackage()->GetMetaData();
    if (!Asset || Metadata->GetValue(Asset, DigestKey) != Collaboration::ContentDigest(Context.Recipe) ||
        Metadata->GetValue(Asset, RevisionKey) != Revision(Asset))
        return Fail(Error, TEXT("output_conflict: choose a new output_asset; existing assets are preserved"));
    return true;
}

FObject Inspect(UStaticMesh* Asset)
{
    FObject Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Asset->GetPathName());
    Result->SetStringField(TEXT("revision"), Revision(Asset));
    Result->SetBoolField(TEXT("dirty"), Asset->GetPackage()->IsDirty());
    const FString Text = Asset->GetPackage()->GetMetaData()->GetValue(Asset, RecipeKey);
    FObject Recipe;
    if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Recipe)) Result->SetObjectField(TEXT("recipe"), Recipe);
    return Result;
}

static void Attributes(FMesh& Mesh)
{
    using namespace UE::Geometry;
    if (!Mesh.HasAttributes()) Mesh.EnableAttributes();
    auto* Attributes = Mesh.Attributes();
    auto* Normals = Attributes->PrimaryNormals();
    if (Normals->ElementCount() == 0) FMeshNormals::InitializeOverlayToPerVertexNormals(Normals, false);
    else
    {
        FMeshNormals Computed(&Mesh);
        Computed.RecomputeOverlayNormals(Normals);
        Computed.CopyToOverlay(Normals);
    }
    if (Attributes->PrimaryUV()->ElementCount() == 0)
    {
        auto* UV = Attributes->PrimaryUV();
        const auto Bounds = Mesh.GetBounds();
        const FVector3d Size = Bounds.Max - Bounds.Min;
        TMap<int32, int32> Map;
        for (int32 ID : Mesh.VertexIndicesItr())
        {
            const FVector3d P = Mesh.GetVertex(ID) - Bounds.Min;
            Map.Add(ID, UV->AppendElement(FVector2f(P.X / FMath::Max(Size.X, 1.0), P.Y / FMath::Max(Size.Y, 1.0))));
        }
        for (int32 ID : Mesh.TriangleIndicesItr())
        {
            const FIndex3i T = Mesh.GetTriangle(ID);
            UV->SetTriangle(ID, FIndex3i(Map[T.A], Map[T.B], Map[T.C]));
        }
    }
}

bool Publish(FContext& Context, const FString& Path, const FString& RecipeText, bool Save, FObject& Result, FString& Error)
{
    const FString ObjectPath = Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
    const FString Digest = Collaboration::ContentDigest(Context.Recipe);
    if (UObject* Existing = LoadObject<UObject>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        UStaticMesh* Asset = Cast<UStaticMesh>(Existing);
        UMetaData* Metadata = Existing->GetPackage()->GetMetaData();
        if (!Asset || Metadata->GetValue(Asset, DigestKey) != Digest || Metadata->GetValue(Asset, RevisionKey) != Revision(Asset))
            return Fail(Error, TEXT("output_conflict: choose a new output_asset; existing assets are preserved"));
        Result = Inspect(Asset);
        Result->SetBoolField(TEXT("reused"), true);
        Result->SetBoolField(TEXT("applied"), false);
        Result->SetBoolField(TEXT("ready"), true);
        const bool Saved = Save && Transcode::SavePackageDirect(Asset->GetPackage(), Asset, Error);
        Result->SetBoolField(TEXT("saved"), Saved);
        Result->SetBoolField(TEXT("dirty"), Asset->GetPackage()->IsDirty());
        return !Save || Saved;
    }
    Attributes(Context.Mesh);
    FMeshDescription Description;
    FStaticMeshAttributes(Description).Register();
    FDynamicMeshToMeshDescription Converter;
    Converter.Convert(&Context.Mesh, Description);
    // Stage in the transient package without RF_Transient: that flag propagates
    // to the default HiResMeshDescription subobject and would omit it on save
    // even after the parent is promoted, crashing the next asset load.
    UStaticMesh* Asset = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transactional);
    FStaticMeshSourceModel& SourceModel = Asset->AddSourceModel();
    SourceModel.BuildSettings.bRecomputeNormals = false;
    SourceModel.BuildSettings.bRecomputeTangents = true;
    SourceModel.BuildSettings.bGenerateLightmapUVs = false;
    SourceModel.BuildSettings.bUseFullPrecisionUVs = true;
    if (Context.Source) Asset->GetStaticMaterials() = Context.Source->GetStaticMaterials();
    if (Asset->GetStaticMaterials().IsEmpty()) Asset->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface)));
    Asset->CreateMeshDescription(0, MoveTemp(Description));
    Asset->CommitMeshDescription(0);
    Asset->CreateBodySetup();
    Asset->GetBodySetup()->CollisionTraceFlag = CTF_UseComplexAsSimple;
    Asset->GetBodySetup()->bDoubleSidedGeometry = true;
    TArray<FText> BuildErrors;
    Asset->Build(true, &BuildErrors);
    if (!BuildErrors.IsEmpty() || Asset->GetNumLODs() == 0)
        return Fail(Error, TEXT("static mesh build failed: ") + (BuildErrors.IsEmpty() ? TEXT("missing LOD") : BuildErrors[0].ToString()));
    Asset->GetBodySetup()->CreatePhysicsMeshes();
    if (Asset->GetBodySetup()->bFailedToCreatePhysicsMeshes || !Asset->GetBodySetup()->bCreatedPhysicsMeshes)
        return Fail(Error, TEXT("collision cooking failed"));
    FinishRenderingUpdates();
    if (Context.Source && Revision(Context.Source) != Context.SourceRevision) return Fail(Error, TEXT("source_conflict during compute"));
    UPackage* Package = CreatePackage(*Path);
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Nexus geometry result")));
        Package->Modify();
        if (!Asset->Rename(*FPackageName::GetLongPackageAssetName(Path), Package, REN_DontCreateRedirectors))
            return Fail(Error, TEXT("could not name output asset"));
        Asset->ClearFlags(RF_Transient);
        Asset->SetFlags(RF_Public | RF_Standalone | RF_Transactional);
        Asset->Modify();
        UMetaData* Metadata = Package->GetMetaData();
        Metadata->SetValue(Asset, RecipeKey, *RecipeText);
        Metadata->SetValue(Asset, DigestKey, *Digest);
        Metadata->SetValue(Asset, RevisionKey, *Revision(Asset));
        Asset->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Asset);
    }
    Result = Inspect(Asset);
    Result->SetBoolField(TEXT("applied"), true);
    Result->SetBoolField(TEXT("ready"), true);
    Result->SetStringField(TEXT("collision"), TEXT("complex_as_simple"));
    const bool Saved = Save && Transcode::SavePackageDirect(Package, Asset, Error);
    Result->SetBoolField(TEXT("saved"), Saved);
    Result->SetBoolField(TEXT("dirty"), Package->IsDirty());
    return !Save || Saved;
}
}
