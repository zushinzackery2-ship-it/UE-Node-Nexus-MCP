#include "NexusValueSchema.h"

#include "Dom/JsonObject.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
static thread_local TSet<const UScriptStruct*> ActiveStructs;

TSharedPtr<FJsonObject> StructValueSchema(UScriptStruct* Struct)
{
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("type"), Struct->GetStructCPPName());
    Result->SetStringField(TEXT("kind"), TEXT("struct"));
    Result->SetStringField(TEXT("path"), Struct->GetPathName());
    if (ActiveStructs.Contains(Struct))
    {
        Result->SetBoolField(TEXT("recursive"), true);
        return Result;
    }
    ActiveStructs.Add(Struct);
    auto Fields = MakeShared<FJsonObject>();
    FStructOnScope Defaults(Struct);
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        for (int32 Index = 0; Index < It->ArrayDim; ++Index)
        {
            auto Field = ValueSchema(*It);
            if (!It->ShouldPort(PPF_None))
            {
                Field->SetBoolField(TEXT("text_importable"), false);
            }
            FString Text;
            It->ExportTextItem_Direct(Text, It->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory(), Index), nullptr, nullptr, PPF_None);
            if (!It->HasMetaData(TEXT("IgnoreForMemberInitializationTest")))
            {
                Field->SetStringField(TEXT("default"), Text);
            }
            else
            {
                Field->SetBoolField(TEXT("generated_default"), true);
            }
            FString Name = It->GetName();
            if (It->ArrayDim > 1)
            {
                Name += FString::Printf(TEXT("[%d]"), Index);
                Field->SetNumberField(TEXT("array_dim"), It->ArrayDim);
                Field->SetNumberField(TEXT("array_index"), Index);
            }
            Fields->SetObjectField(Name, Field);
        }
    }
    Result->SetObjectField(TEXT("fields"), Fields);
    ActiveStructs.Remove(Struct);
    return Result;
}

TSharedPtr<FJsonObject> ValueSchema(FProperty* Property)
{
    if (auto* Struct = CastField<FStructProperty>(Property))
    {
        return StructValueSchema(Struct->Struct);
    }
    auto Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("type"), CastField<FBoolProperty>(Property) ? TEXT("bool") : Property->GetCPPType());
    if (auto* Array = CastField<FArrayProperty>(Property))
    {
        Result->SetStringField(TEXT("kind"), TEXT("array"));
        Result->SetObjectField(TEXT("element"), ValueSchema(Array->Inner));
    }
    else if (CastField<FObjectPropertyBase>(Property) || CastField<FSoftObjectProperty>(Property))
    {
        Result->SetStringField(TEXT("kind"), TEXT("object"));
    }
    return Result;
}
}
