#include "Blueprint/CallHost/NexusCallHostClass.h"

#include "EdGraphSchema_K2.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallDataTableFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallMaterialParameterCollectionFunction.h"

namespace UeNodeNexusBridge
{
bool IsCallHostClass(const UClass* NodeClass)
{
    return NodeClass == UK2Node_CallFunction::StaticClass()
        || NodeClass == UK2Node_CallArrayFunction::StaticClass()
        || NodeClass == UK2Node_CallDataTableFunction::StaticClass()
        || NodeClass == UK2Node_CallMaterialParameterCollectionFunction::StaticClass();
}

UClass* CallHostClassFor(const UFunction* Function)
{
    // Same precedence as UBlueprintFunctionNodeSpawner::Create. The operator
    // classes it also picks are left out: they have their own mirror spelling,
    // and a plain call node for an operator function compiles.
    if (Function == nullptr)
    {
        return UK2Node_CallFunction::StaticClass();
    }
    if (Function->HasMetaData(FBlueprintMetadata::MD_MaterialParameterCollectionFunction))
    {
        return UK2Node_CallMaterialParameterCollectionFunction::StaticClass();
    }
    if (Function->HasMetaData(FBlueprintMetadata::MD_DataTablePin))
    {
        return UK2Node_CallDataTableFunction::StaticClass();
    }
    if (Function->HasMetaData(FBlueprintMetadata::MD_ArrayParam))
    {
        return UK2Node_CallArrayFunction::StaticClass();
    }
    return UK2Node_CallFunction::StaticClass();
}

UFunction* FindCallFunction(const FString& OwnerPath, const FString& FunctionName)
{
    if (OwnerPath.IsEmpty() || FunctionName.IsEmpty())
    {
        return nullptr;
    }
    UClass* Owner = FindObject<UClass>(nullptr, *OwnerPath);
    if (Owner == nullptr)
    {
        Owner = LoadClass<UObject>(nullptr, *OwnerPath);
    }
    return Owner ? Owner->FindFunctionByName(FName(*FunctionName)) : nullptr;
}

bool SelectCallHostClass(UClass*& InOutClass, const UFunction* Function, FString& OutError)
{
    if (!IsCallHostClass(InOutClass))
    {
        return true;
    }
    if (Function == nullptr)
    {
        if (InOutClass == UK2Node_CallFunction::StaticClass())
        {
            return true;
        }
        OutError = FString::Printf(TEXT("%s needs a resolvable function to confirm its host class"), *InOutClass->GetName());
        return false;
    }
    UClass* Required = CallHostClassFor(Function);
    if (InOutClass != UK2Node_CallFunction::StaticClass() && InOutClass != Required)
    {
        OutError = FString::Printf(TEXT("%s cannot call %s.%s; its metadata requires %s (write CallFunction and the bridge chooses)"),
            *InOutClass->GetName(), *Function->GetOwnerClass()->GetName(), *Function->GetName(), *Required->GetName());
        return false;
    }
    InOutClass = Required;
    return true;
}

bool HasRequiredCallHost(const UEdGraphNode* Node)
{
    const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
    if (Call == nullptr || !IsCallHostClass(Node->GetClass()))
    {
        return true;
    }
    const UFunction* Function = Call->GetTargetFunction();
    return Function == nullptr || CallHostClassFor(Function) == Node->GetClass();
}
}
