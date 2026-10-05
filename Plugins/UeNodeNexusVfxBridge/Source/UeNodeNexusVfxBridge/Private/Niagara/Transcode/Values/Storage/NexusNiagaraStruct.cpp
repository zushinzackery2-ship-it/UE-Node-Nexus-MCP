#include "NexusNiagaraStorage.h"

#include "NiagaraTypes.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::VfxTranscode::Storage
{
static bool NumericStructure(const UStruct* Struct, int32 Depth)
{
    if (Struct == nullptr || Depth > 8)
    {
        return false;
    }
    for (TFieldIterator<FProperty> Field(Struct); Field; ++Field)
    {
        const FProperty* Property = *Field;
        if (Property->IsA<FNumericProperty>() || Property->IsA<FBoolProperty>() || Property->IsA<FEnumProperty>())
        {
            continue;
        }
        const FStructProperty* Nested = CastField<FStructProperty>(Property);
        if (Nested == nullptr || !NumericStructure(Nested->Struct, Depth + 1))
        {
            return false;
        }
    }
    return true;
}

bool ReadStructText(const FNiagaraParameterStore& Store, const FNiagaraVariable& Variable,
    FString& Text, FString& Error)
{
    const FNiagaraTypeDefinition& Type = Variable.GetType();
    UScriptStruct* Struct = Type.IsValid() ? Type.GetScriptStruct() : nullptr;
    if (!NumericStructure(Struct, 0))
    {
        Error = FString::Printf(TEXT("parameter=%s: stored structure requires numeric fields"), *Variable.GetName().ToString());
        return false;
    }
    // The reflected allocation owns the alignment, initialization and lifetime.
    // Copy only verified numeric fields; never deserialize reference containers.
    FStructOnScope Value(Struct);
    if (!ReadBytes(Store, Variable, Value.GetStructMemory(), Struct->GetStructureSize(), Error))
    {
        return false;
    }
    Struct->ExportText(Text, Value.GetStructMemory(), nullptr, nullptr, PPF_None, nullptr);
    return true;
}
}
