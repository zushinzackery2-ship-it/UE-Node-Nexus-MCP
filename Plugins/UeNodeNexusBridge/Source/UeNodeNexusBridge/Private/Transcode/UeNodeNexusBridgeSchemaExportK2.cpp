#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "Schema/NexusSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
// Pin templates are deliberately not extracted here: instantiating K2 nodes
// outside a Blueprint asserts in FindBlueprintForNodeChecked. Pins of Blueprint
// nodes are therefore reported as dynamic and validated live on push.
static TSharedPtr<FJsonObject> ClassProps(UClass* Class)
{
    TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
    UObject* Cdo = Class->GetDefaultObject();
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        if (IsEditableProperty(*It))
        {
            Props->SetObjectField(It->GetName(), PropertySchemaJson(*It, Cdo));
        }
    }
    return Props;
}

TSharedPtr<FJsonObject> BuildK2NodeSchema()
{
    TSharedPtr<FJsonObject> Classes = MakeShared<FJsonObject>();
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        const bool bK2 = Class->IsChildOf(UK2Node::StaticClass());
        const bool bComment = Class == UEdGraphNode_Comment::StaticClass();
        if ((!bK2 && !bComment) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            continue;
        }
        if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
        {
            continue;
        }
        TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Class->GetPathName());
        Record->SetObjectField(TEXT("props"), ClassProps(Class));
        Record->SetArrayField(TEXT("pins"), TArray<TSharedPtr<FJsonValue>>());
        Record->SetBoolField(TEXT("dynamic_pins"), true);
        AddClassMetadata(Class, Record);
        Classes->SetObjectField(Class->GetPathName(), Record);
    }
    return Classes;
}

TSharedPtr<FJsonObject> BuildFunctionSignatureRecord(UFunction* Function)
{
    TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
    if (Function == nullptr)
    {
        return Record;
    }
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    TArray<TSharedPtr<FJsonValue>> Params;
    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        FProperty* Property = *It;
        FEdGraphPinType PinType;
        Schema->ConvertPropertyToPinType(Property, PinType);
        TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
        Param->SetStringField(TEXT("name"), Property->GetName());
        Param->SetObjectField(TEXT("type"), PinTypeJson(PinType));
        const bool bOut = Property->HasAnyPropertyFlags(CPF_ReturnParm) || (Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm));
        Param->SetStringField(TEXT("dir"), bOut ? TEXT("out") : TEXT("in"));
        const FString DefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName());
        Param->SetStringField(TEXT("default"), Function->HasMetaData(*DefaultKey) ? Function->GetMetaData(*DefaultKey) : FString());
        Param->SetStringField(TEXT("default_source"), Function->HasMetaData(*DefaultKey) ? TEXT("CPP_Default_metadata") : TEXT("metadata_not_provided"));
        Param->SetStringField(TEXT("tooltip"), Property->GetToolTipText().ToString());
        Params.Add(MakeShared<FJsonValueObject>(Param));
    }
    Record->SetArrayField(TEXT("params"), Params);
    Record->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
    Record->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
    Record->SetStringField(TEXT("owner"), Function->GetOwnerClass() ? Function->GetOwnerClass()->GetPathName() : FString());
    Record->SetStringField(TEXT("path"), Function->GetOwnerClass()->GetPathName() + TEXT(".") + Function->GetName());
    Record->SetStringField(TEXT("tooltip"), Function->GetToolTipText().ToString());
    Record->SetStringField(TEXT("coverage"), TEXT("signature"));
    Record->SetBoolField(TEXT("deprecated"), Function->HasMetaData(TEXT("DeprecatedFunction")));
    const auto Conditions = MakeShared<FJsonObject>();
    for (const TCHAR* Key : { TEXT("WorldContext"), TEXT("DeterminesOutputType"), TEXT("DynamicOutputParam"), TEXT("BlueprintInternalUseOnly"), TEXT("Latent"), TEXT("DeprecatedFunction") })
    {
        if (Function->HasMetaData(Key))
        {
            Conditions->SetStringField(Key, Function->GetMetaData(Key));
        }
    }
    Record->SetObjectField(TEXT("conditions"), Conditions);
    return Record;
}
}
