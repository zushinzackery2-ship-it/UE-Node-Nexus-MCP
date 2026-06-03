#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FRawDistributionFloat;
struct FRawDistributionVector;
class UParticleModule;

namespace UeNodeNexusBridge
{
// Normalizes a Cascade float distribution to a typed JSON value, reading the
// authored UDistributionFloat subclass (no runtime baking):
//   {type:"constant", value} | {type:"uniform", min, max} |
//   {type:"curve", keys:[{time, value}]} | {type:"<ClassName>"} fallback.
TSharedPtr<FJsonValue> FloatDistributionToJson(const FRawDistributionFloat& RawDistribution);

// Vector counterpart. value/min/max are {x,y,z} objects; curve keys carry {x,y,z}.
TSharedPtr<FJsonValue> VectorDistributionToJson(const FRawDistributionVector& RawDistribution);

// Returns a {param_name -> value} object of the common authored parameters for a
// supported Cascade module class (Spawn/Lifetime/Size/Color/Velocity/Location/
// Rotation/Light), or an empty object for modules with no normalized mapping.
TSharedPtr<FJsonObject> ModuleValuesToJson(UParticleModule* Module);

// Compact one-line "key=value;key2=value2" digest of ModuleValuesToJson for the
// self-describing compact module rows. Empty for unsupported modules.
FString ModuleValuesToCompactString(UParticleModule* Module);
}
