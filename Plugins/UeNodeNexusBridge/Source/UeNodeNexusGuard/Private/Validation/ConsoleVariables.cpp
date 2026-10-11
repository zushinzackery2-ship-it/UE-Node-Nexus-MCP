#include "Environment.h"

#include "HAL/IConsoleManager.h"

namespace NexusLifecycle::Validation
{
namespace
{
bool RenderingVariable(const FString& Name)
{
    for (const TCHAR* Prefix : {TEXT("r."), TEXT("sg."), TEXT("fx."), TEXT("niagara."),
        TEXT("rhi."), TEXT("d3d11."), TEXT("d3d12."), TEXT("p."), TEXT("a."), TEXT("foliage."), TEXT("grass.")})
    {
        if (Name.StartsWith(Prefix, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}
}

TSharedPtr<FJsonObject> ConsoleVariables()
{
    auto Result = MakeShared<FJsonObject>();
    IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(FConsoleObjectVisitor::CreateLambda(
        [Result](const TCHAR* Name, IConsoleObject* Object)
        {
            IConsoleVariable* Variable = Object->AsVariable();
            if (Variable && RenderingVariable(Name) && !Variable->TestFlags(ECVF_Unregistered))
            {
                Result->SetStringField(Name, Normalize(Variable->GetString()));
            }
        }));
    return Result;
}

bool ValidateConsoleVariables(const TSharedPtr<FJsonObject>& Input, FString& Error)
{
    if (!Input.IsValid() || Input->Values.IsEmpty())
    {
        Error = TEXT("rendering console variable measurement is empty");
        return false;
    }
    for (const auto& Pair : Input->Values)
    {
        FString Value;
        if (!RenderingVariable(Pair.Key) || !Pair.Value->TryGetString(Value))
        {
            Error = TEXT("reproduction requires a rendering variable and string value");
            return false;
        }
    }
    return true;
}

TArray<FString> ApplyConsoleVariables(const TSharedPtr<FJsonObject>& Input)
{
    auto& Manager = IConsoleManager::Get();
    TArray<FString> Pending;
    for (const auto& Pair : Input->Values)
    {
        const FString Value = Expand(Pair.Value->AsString());
        IConsoleVariable* Variable = Manager.FindConsoleVariable(*Pair.Key, false);
        if (!Variable)
        {
            // UE transfers ini-created values when the owning module registers
            // its real variable. Console values already have their native text.
            Manager.RegisterConsoleVariable(*Pair.Key, *Value, TEXT("Nexus validation startup value"),
                ECVF_Unregistered | ECVF_CreatedFromIni | ECVF_SetByConsole);
            Pending.Add(Pair.Key);
            continue;
        }
        if (Variable->GetString() != Value)
        {
            Pending.Add(Pair.Key);
        }
        // PostConfigInit precedes UObject initialization as well as RHIInit.
        // Value propagation is safe here; OnChanged delegates may create UObjects.
        Variable->Set(*Value, static_cast<EConsoleVariableFlags>(ECVF_SetByConsole | ECVF_Set_SetOnly_Unsafe));
        Variable->SetFlags(static_cast<EConsoleVariableFlags>((Variable->GetFlags() & ~ECVF_SetByMask) | ECVF_SetByConsole));
    }
    return Pending;
}

bool CompleteConsoleVariables(const TSharedPtr<FJsonObject>& Input, const TArray<FString>& Pending, FString& Error)
{
    auto& Manager = IConsoleManager::Get();
    for (const FString& Name : Pending)
    {
        const IConsoleVariable* Variable = Manager.FindConsoleVariable(*Name, false);
        if (!Variable || Variable->TestFlags(ECVF_Unregistered))
        {
            Error = TEXT("rendering console variable was not registered during engine initialization: ") + Name;
            return false;
        }
    }
    for (const FString& Name : Pending)
    {
        // Invoke the normal change path only after modules and UObject are ready.
        Manager.FindConsoleVariable(*Name, false)->Set(*Expand(Input->GetStringField(Name)), ECVF_SetByConsole);
    }
    Manager.CallAllConsoleVariableSinks();
    return true;
}
}
