#include "UeNodeNexusBridgeCascadeDistributions.h"

#include "Distributions/DistributionFloatConstant.h"
#include "Distributions/DistributionFloatConstantCurve.h"
#include "Distributions/DistributionFloatUniform.h"
#include "Distributions/DistributionVectorConstant.h"
#include "Distributions/DistributionVectorConstantCurve.h"
#include "Distributions/DistributionVectorUniform.h"
#include "Particles/Color/ParticleModuleColorOverLife.h"
#include "Particles/Lifetime/ParticleModuleLifetime.h"
#include "Particles/Light/ParticleModuleLight.h"
#include "Particles/Location/ParticleModuleLocation.h"
#include "Particles/ParticleModule.h"
#include "Particles/Rotation/ParticleModuleRotation.h"
#include "Particles/Size/ParticleModuleSize.h"
#include "Particles/Spawn/ParticleModuleSpawn.h"
#include "Particles/Velocity/ParticleModuleVelocity.h"
#include "UeNodeNexusBridgeObjectHelpers.h"

namespace UeNodeNexusBridge
{
namespace
{
FString FloatToCompact(float Value)
{
    return FString::SanitizeFloat(Value);
}

FString VectorToCompact(const FVector& Value)
{
    return FString::Printf(TEXT("(%s,%s,%s)"),
        *FString::SanitizeFloat(Value.X), *FString::SanitizeFloat(Value.Y), *FString::SanitizeFloat(Value.Z));
}

FString FloatDistToCompact(const FRawDistributionFloat& RawDistribution)
{
    UDistributionFloat* Distribution = RawDistribution.Distribution;
    if (Distribution == nullptr)
    {
        return TEXT("none");
    }
    if (const UDistributionFloatConstant* Constant = Cast<UDistributionFloatConstant>(Distribution))
    {
        return FloatToCompact(Constant->Constant);
    }
    if (const UDistributionFloatUniform* Uniform = Cast<UDistributionFloatUniform>(Distribution))
    {
        return FString::Printf(TEXT("%s..%s"), *FloatToCompact(Uniform->Min), *FloatToCompact(Uniform->Max));
    }
    if (const UDistributionFloatConstantCurve* Curve = Cast<UDistributionFloatConstantCurve>(Distribution))
    {
        return FString::Printf(TEXT("curve[%d]"), Curve->ConstantCurve.Points.Num());
    }
    return Distribution->GetClass()->GetName();
}

FString VectorDistToCompact(const FRawDistributionVector& RawDistribution)
{
    UDistributionVector* Distribution = RawDistribution.Distribution;
    if (Distribution == nullptr)
    {
        return TEXT("none");
    }
    if (const UDistributionVectorConstant* Constant = Cast<UDistributionVectorConstant>(Distribution))
    {
        return VectorToCompact(Constant->Constant);
    }
    if (const UDistributionVectorUniform* Uniform = Cast<UDistributionVectorUniform>(Distribution))
    {
        return FString::Printf(TEXT("%s..%s"), *VectorToCompact(Uniform->Min), *VectorToCompact(Uniform->Max));
    }
    if (const UDistributionVectorConstantCurve* Curve = Cast<UDistributionVectorConstantCurve>(Distribution))
    {
        return FString::Printf(TEXT("curve[%d]"), Curve->ConstantCurve.Points.Num());
    }
    return Distribution->GetClass()->GetName();
}
}

TSharedPtr<FJsonValue> FloatDistributionToJson(const FRawDistributionFloat& RawDistribution)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    UDistributionFloat* Distribution = RawDistribution.Distribution;
    if (Distribution == nullptr)
    {
        Json->SetStringField(TEXT("type"), TEXT("none"));
        return MakeShared<FJsonValueObject>(Json);
    }
    if (const UDistributionFloatConstant* Constant = Cast<UDistributionFloatConstant>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("constant"));
        Json->SetNumberField(TEXT("value"), Constant->Constant);
    }
    else if (const UDistributionFloatUniform* Uniform = Cast<UDistributionFloatUniform>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("uniform"));
        Json->SetNumberField(TEXT("min"), Uniform->Min);
        Json->SetNumberField(TEXT("max"), Uniform->Max);
    }
    else if (const UDistributionFloatConstantCurve* Curve = Cast<UDistributionFloatConstantCurve>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("curve"));
        TArray<TSharedPtr<FJsonValue>> Keys;
        for (const FInterpCurvePoint<float>& Point : Curve->ConstantCurve.Points)
        {
            TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
            Key->SetNumberField(TEXT("time"), Point.InVal);
            Key->SetNumberField(TEXT("value"), Point.OutVal);
            Keys.Add(MakeShared<FJsonValueObject>(Key));
        }
        Json->SetArrayField(TEXT("keys"), Keys);
    }
    else
    {
        Json->SetStringField(TEXT("type"), Distribution->GetClass()->GetName());
    }
    return MakeShared<FJsonValueObject>(Json);
}

TSharedPtr<FJsonValue> VectorDistributionToJson(const FRawDistributionVector& RawDistribution)
{
    TSharedPtr<FJsonObject> Json = MakeShared<FJsonObject>();
    UDistributionVector* Distribution = RawDistribution.Distribution;
    if (Distribution == nullptr)
    {
        Json->SetStringField(TEXT("type"), TEXT("none"));
        return MakeShared<FJsonValueObject>(Json);
    }
    if (const UDistributionVectorConstant* Constant = Cast<UDistributionVectorConstant>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("constant"));
        Json->SetObjectField(TEXT("value"), MakeVectorJson(Constant->Constant));
    }
    else if (const UDistributionVectorUniform* Uniform = Cast<UDistributionVectorUniform>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("uniform"));
        Json->SetObjectField(TEXT("min"), MakeVectorJson(Uniform->Min));
        Json->SetObjectField(TEXT("max"), MakeVectorJson(Uniform->Max));
    }
    else if (const UDistributionVectorConstantCurve* Curve = Cast<UDistributionVectorConstantCurve>(Distribution))
    {
        Json->SetStringField(TEXT("type"), TEXT("curve"));
        TArray<TSharedPtr<FJsonValue>> Keys;
        for (const FInterpCurvePoint<FVector>& Point : Curve->ConstantCurve.Points)
        {
            TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
            Key->SetNumberField(TEXT("time"), Point.InVal);
            Key->SetObjectField(TEXT("value"), MakeVectorJson(Point.OutVal));
            Keys.Add(MakeShared<FJsonValueObject>(Key));
        }
        Json->SetArrayField(TEXT("keys"), Keys);
    }
    else
    {
        Json->SetStringField(TEXT("type"), Distribution->GetClass()->GetName());
    }
    return MakeShared<FJsonValueObject>(Json);
}

TSharedPtr<FJsonObject> ModuleValuesToJson(UParticleModule* Module)
{
    TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
    if (Module == nullptr)
    {
        return Values;
    }

    if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
    {
        Values->SetField(TEXT("rate"), FloatDistributionToJson(Spawn->Rate));
        Values->SetField(TEXT("rate_scale"), FloatDistributionToJson(Spawn->RateScale));
        Values->SetField(TEXT("burst_scale"), FloatDistributionToJson(Spawn->BurstScale));
        Values->SetBoolField(TEXT("apply_global_spawn_rate_scale"), Spawn->bApplyGlobalSpawnRateScale != 0);
        TArray<TSharedPtr<FJsonValue>> Bursts;
        for (const FParticleBurst& Burst : Spawn->BurstList)
        {
            TSharedPtr<FJsonObject> BurstJson = MakeShared<FJsonObject>();
            BurstJson->SetNumberField(TEXT("count"), Burst.Count);
            BurstJson->SetNumberField(TEXT("count_low"), Burst.CountLow);
            BurstJson->SetNumberField(TEXT("time"), Burst.Time);
            Bursts.Add(MakeShared<FJsonValueObject>(BurstJson));
        }
        Values->SetArrayField(TEXT("burst_list"), Bursts);
    }
    else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
    {
        Values->SetField(TEXT("lifetime"), FloatDistributionToJson(Lifetime->Lifetime));
    }
    else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
    {
        Values->SetField(TEXT("start_size"), VectorDistributionToJson(Size->StartSize));
    }
    else if (UParticleModuleColorOverLife* Color = Cast<UParticleModuleColorOverLife>(Module))
    {
        Values->SetField(TEXT("color_over_life"), VectorDistributionToJson(Color->ColorOverLife));
        Values->SetField(TEXT("alpha_over_life"), FloatDistributionToJson(Color->AlphaOverLife));
        Values->SetBoolField(TEXT("clamp_alpha"), Color->bClampAlpha != 0);
    }
    else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
    {
        Values->SetField(TEXT("start_velocity"), VectorDistributionToJson(Velocity->StartVelocity));
        Values->SetField(TEXT("start_velocity_radial"), FloatDistributionToJson(Velocity->StartVelocityRadial));
    }
    else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
    {
        Values->SetField(TEXT("start_location"), VectorDistributionToJson(Location->StartLocation));
    }
    else if (UParticleModuleRotation* Rotation = Cast<UParticleModuleRotation>(Module))
    {
        Values->SetField(TEXT("start_rotation"), FloatDistributionToJson(Rotation->StartRotation));
    }
    else if (UParticleModuleLight* Light = Cast<UParticleModuleLight>(Module))
    {
        Values->SetNumberField(TEXT("spawn_fraction"), Light->SpawnFraction);
        Values->SetField(TEXT("color_scale_over_life"), VectorDistributionToJson(Light->ColorScaleOverLife));
        Values->SetField(TEXT("brightness_over_life"), FloatDistributionToJson(Light->BrightnessOverLife));
        Values->SetField(TEXT("radius_scale"), FloatDistributionToJson(Light->RadiusScale));
        Values->SetField(TEXT("light_exponent"), FloatDistributionToJson(Light->LightExponent));
    }
    return Values;
}

FString ModuleValuesToCompactString(UParticleModule* Module)
{
    if (Module == nullptr)
    {
        return FString();
    }

    TArray<FString> Parts;
    if (UParticleModuleSpawn* Spawn = Cast<UParticleModuleSpawn>(Module))
    {
        Parts.Add(FString::Printf(TEXT("rate=%s"), *FloatDistToCompact(Spawn->Rate)));
        Parts.Add(FString::Printf(TEXT("rate_scale=%s"), *FloatDistToCompact(Spawn->RateScale)));
        Parts.Add(FString::Printf(TEXT("bursts=%d"), Spawn->BurstList.Num()));
    }
    else if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
    {
        Parts.Add(FString::Printf(TEXT("lifetime=%s"), *FloatDistToCompact(Lifetime->Lifetime)));
    }
    else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
    {
        Parts.Add(FString::Printf(TEXT("start_size=%s"), *VectorDistToCompact(Size->StartSize)));
    }
    else if (UParticleModuleColorOverLife* Color = Cast<UParticleModuleColorOverLife>(Module))
    {
        Parts.Add(FString::Printf(TEXT("color_over_life=%s"), *VectorDistToCompact(Color->ColorOverLife)));
        Parts.Add(FString::Printf(TEXT("alpha_over_life=%s"), *FloatDistToCompact(Color->AlphaOverLife)));
    }
    else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
    {
        Parts.Add(FString::Printf(TEXT("start_velocity=%s"), *VectorDistToCompact(Velocity->StartVelocity)));
        Parts.Add(FString::Printf(TEXT("start_velocity_radial=%s"), *FloatDistToCompact(Velocity->StartVelocityRadial)));
    }
    else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
    {
        Parts.Add(FString::Printf(TEXT("start_location=%s"), *VectorDistToCompact(Location->StartLocation)));
    }
    else if (UParticleModuleRotation* Rotation = Cast<UParticleModuleRotation>(Module))
    {
        Parts.Add(FString::Printf(TEXT("start_rotation=%s"), *FloatDistToCompact(Rotation->StartRotation)));
    }
    else if (UParticleModuleLight* Light = Cast<UParticleModuleLight>(Module))
    {
        Parts.Add(FString::Printf(TEXT("color_scale_over_life=%s"), *VectorDistToCompact(Light->ColorScaleOverLife)));
        Parts.Add(FString::Printf(TEXT("brightness_over_life=%s"), *FloatDistToCompact(Light->BrightnessOverLife)));
        Parts.Add(FString::Printf(TEXT("radius_scale=%s"), *FloatDistToCompact(Light->RadiusScale)));
    }
    return FString::Join(Parts, TEXT(";"));
}
}
