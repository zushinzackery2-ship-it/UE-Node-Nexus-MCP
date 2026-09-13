#include "NexusSchema.h"

#include "UeNodeNexusBridgeTranscode.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> CallableFunctionIndex()
{
    const auto Result = MakeShared<FJsonObject>();
    for (TObjectIterator<UClass> Class; Class; ++Class)
    {
        if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
            || Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
        {
            continue;
        }
        for (TFieldIterator<UFunction> Function(*Class, EFieldIteratorFlags::ExcludeSuper); Function; ++Function)
        {
            if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
            {
                continue;
            }
            const FString Name = Class->GetPathName() + TEXT(".") + Function->GetName();
            const auto Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("path"), Name);
            Record->SetStringField(TEXT("owner"), Class->GetPathName());
            Record->SetStringField(TEXT("tooltip"), Function->GetToolTipText().ToString());
            Record->SetStringField(TEXT("coverage"), TEXT("index_only"));
            Record->SetStringField(TEXT("detail_query"), Name);
            Record->SetBoolField(TEXT("engine_available"), true);
            Record->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
            Record->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
            Record->SetBoolField(TEXT("deprecated"), Function->HasMetaData(TEXT("DeprecatedFunction")));
            const auto Support = MakeShared<FJsonObject>();
            Support->SetBoolField(TEXT("inspect"), true);
            Support->SetStringField(TEXT("create"), TEXT("context_required"));
            Support->SetStringField(TEXT("write"), TEXT("context_required"));
            Support->SetStringField(TEXT("delete"), TEXT("call_node_only"));
            Support->SetStringField(TEXT("entry"), TEXT("transcode_apply/K2Node_CallFunction"));
            Support->SetStringField(TEXT("source"), TEXT("BlueprintCallable reflection; graph validation required"));
            Record->SetObjectField(TEXT("bridge"), Support);
            Result->SetObjectField(Name, Record);
        }
    }
    return Result;
}

TSharedPtr<FJsonObject> CommonTypes()
{
    const auto Result = MakeShared<FJsonObject>();
    for (TObjectIterator<UEnum> Enum; Enum; ++Enum)
    {
        const auto Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Enum->GetPathName());
        Record->SetStringField(TEXT("kind"), TEXT("enum"));
        Record->SetStringField(TEXT("tooltip"), Enum->GetToolTipText().ToString());
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
        {
            if (!Enum->HasMetaData(TEXT("Hidden"), Index))
            {
                Values.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
            }
        }
        Record->SetArrayField(TEXT("enum_values"), Values);
        Result->SetObjectField(Enum->GetPathName(), Record);
    }
    for (TObjectIterator<UScriptStruct> Struct; Struct; ++Struct)
    {
        const auto Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Struct->GetPathName());
        Record->SetStringField(TEXT("kind"), TEXT("struct"));
        Record->SetStringField(TEXT("tooltip"), Struct->GetToolTipText().ToString());
        const auto Props = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> Property(*Struct); Property; ++Property)
        {
            Props->SetObjectField(Property->GetName(), PropertySchemaJson(*Property, nullptr));
        }
        Record->SetObjectField(TEXT("props"), Props);
        Result->SetObjectField(Struct->GetPathName(), Record);
    }
    return Result;
}
}
