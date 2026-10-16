#include "NexusDelegateSchema.h"

#include "Dom/JsonObject.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UeNodeNexusBridgeTranscode.h"

namespace UeNodeNexusBridge::Transcode
{
TSharedPtr<FJsonObject> BuildDelegateSchema()
{
    const auto Records = MakeShared<FJsonObject>();
    for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
    {
        UClass* Class = *ClassIt;
        if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
            || Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
        {
            continue;
        }
        for (TFieldIterator<FMulticastDelegateProperty> PropertyIt(Class, EFieldIteratorFlags::ExcludeSuper);
            PropertyIt; ++PropertyIt)
        {
            const auto Property = *PropertyIt;
            if (Property->SignatureFunction == nullptr)
            {
                continue;
            }
            const FString Path = Class->GetPathName() + TEXT(".") + Property->GetName();
            const auto Record = BuildFunctionSignatureRecord(Property->SignatureFunction);
            Record->SetStringField(TEXT("path"), Path);
            Record->SetStringField(TEXT("owner"), Class->GetPathName());
            Record->SetBoolField(TEXT("callable"), Property->HasAnyPropertyFlags(CPF_BlueprintCallable));
            Record->SetBoolField(TEXT("assignable"), Property->HasAnyPropertyFlags(CPF_BlueprintAssignable));
            Record->SetStringField(TEXT("coverage"), TEXT("multicast_delegate_signature"));
            Records->SetObjectField(Path, Record);
        }
    }
    return Records;
}
}
