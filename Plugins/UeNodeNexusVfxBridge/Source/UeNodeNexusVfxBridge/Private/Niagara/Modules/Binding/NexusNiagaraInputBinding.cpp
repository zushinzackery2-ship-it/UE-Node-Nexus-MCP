#include "NexusNiagaraInputBinding.h"
#include "NexusNiagaraInputMetadata.h"

#include "Internationalization/Regex.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"

namespace UeNodeNexusBridge::VfxTranscode
{
bool ResolveModuleInput(UNiagaraNodeFunctionCall& Module, const FString& Name,
    FNiagaraVariable& Variable, FInputBindingError& Error)
{
    UNiagaraGraph* Graph = Module.GetCalledGraph();
    if (!Graph)
    {
        Error.Code = TEXT("input_graph_missing");
        Error.Message = TEXT("module called graph is unavailable");
        return false;
    }
    const FName Requested(*Name);
    const FName Qualified(*((Name.StartsWith(TEXT("Module."))) ? Name : TEXT("Module.") + Name));
    int32 Matches = 0;
    for (const auto& Pair : Graph->GetAllMetaData())
    {
        if (IsAuthoredModuleInput(Pair.Key, Pair.Value)
            && (Pair.Key.GetName() == Requested || Pair.Key.GetName() == Qualified))
        {
            Variable = Pair.Key;
            ++Matches;
        }
    }
    if (Matches != 1)
    {
        Error.Code = TEXT("module_input_unresolved");
        Error.Message = FString::Printf(TEXT("input=%s metadata_matches=%d; require exactly one typed input"), *Name, Matches);
        return false;
    }
    return true;
}

bool ValidateUserParameter(const FNiagaraUserRedirectionParameterStore& Store,
    const FString& Parameter, const FNiagaraTypeDefinition& Type, FInputBindingError& Error)
{
    static const FRegexPattern Pattern(TEXT("User\\.[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)*"));
    FRegexMatcher Matcher(Pattern, Parameter);
    if (!Matcher.FindNext() || Matcher.GetMatchBeginning() != 0 || Matcher.GetMatchEnding() != Parameter.Len())
    {
        Error.Code = TEXT("invalid_user_link");
        Error.Message = TEXT("linked input requires a complete User.Name handle");
        return false;
    }
    TArray<FNiagaraVariable> Variables;
    Store.GetUserParameters(Variables);
    int32 Matches = 0;
    FString Actual;
    bool bExactType = false;
    for (FNiagaraVariable& Variable : Variables)
    {
        FNiagaraUserRedirectionParameterStore::MakeUserVariable(Variable);
        if (Variable.GetName() == FName(*Parameter))
        {
            ++Matches;
            Actual += (Actual.IsEmpty() ? TEXT("") : TEXT(",")) + Variable.GetType().GetName();
            bExactType = Variable.GetType() == Type;
        }
    }
    if (Matches != 1 || !bExactType)
    {
        Error.Code = Matches == 0 ? TEXT("user_parameter_missing") : TEXT("user_parameter_type_mismatch");
        Error.Message = FString::Printf(TEXT("parameter=%s expected_type=%s actual_types=%s name_matches=%d"),
            *Parameter, *Type.GetName(), *Actual, Matches);
        return false;
    }
    return true;
}

bool ValidateLinkedParameter(const FNiagaraUserRedirectionParameterStore& Store,
    UNiagaraStackFunctionInput& Input, const FString& Parameter, FInputBindingError& Error)
{
    if (Parameter.StartsWith(TEXT("User.")))
    {
        return ValidateUserParameter(Store, Parameter, Input.GetInputType(), Error);
    }
    static const FRegexPattern Pattern(TEXT("[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+"));
    FRegexMatcher Matcher(Pattern, Parameter);
    if (!Matcher.FindNext() || Matcher.GetMatchBeginning() != 0 || Matcher.GetMatchEnding() != Parameter.Len())
    {
        Error.Code = TEXT("invalid_parameter_link");
        Error.Message = TEXT("linked input requires a complete Namespace.Name handle");
        return false;
    }
    TArray<FNiagaraParameterHandle> Handles;
    TMap<FNiagaraVariable, UNiagaraScript*> Conversions;
    // Niagara filters by assignable type, execution stage and writes preceding this module.
    Input.GetAvailableParameterHandles(Handles, Conversions, false);
    const FNiagaraParameterHandle Requested = FNiagaraParameterHandle(FName(*Parameter));
    if (Handles.Contains(Requested))
    {
        return true;
    }
    TArray<FString> Available;
    for (const FNiagaraParameterHandle& Handle : Handles)
    {
        Available.AddUnique(Handle.GetParameterHandleString().ToString());
    }
    Available.Sort();
    Error.Code = TEXT("linked_parameter_unavailable");
    Error.Message = FString::Printf(TEXT("parameter=%s expected_type=%s unavailable before target module; available_handles=%s"),
        *Parameter, *Input.GetInputType().GetName(), *FString::Join(Available, TEXT(",")));
    return false;
}

UNiagaraStackEntry* FInputBindingContext::ResolveRoot(UNiagaraSystem& System,
    FNiagaraEmitterHandle& Emitter, FInputBindingError& Error)
{
    if (!ViewModel.IsValid())
    {
        FNiagaraSystemViewModelOptions Options;
        Options.bCanAutoCompile = false;
        Options.bCanSimulate = false;
        Options.bIsForDataProcessingOnly = true;
        Options.MessageLogGuid = FGuid::NewGuid();
        ViewModel = MakeShared<FNiagaraSystemViewModel>();
        ViewModel->Initialize(System, Options);
    }
    for (const TSharedRef<FNiagaraEmitterHandleViewModel>& EmitterVM : ViewModel->GetEmitterHandleViewModels())
    {
        if (EmitterVM->GetEmitterHandle() != &Emitter)
        {
            continue;
        }
        UNiagaraStackEntry* Root = EmitterVM->GetEmitterStackViewModel()->GetRootEntry();
        if (!Root)
        {
            continue;
        }
        Root->RefreshChildren();
        return Root;
    }
    Error.Code = TEXT("stack_root_unresolved");
    Error.Message = TEXT("native emitter stack root is unavailable");
    return nullptr;
}

UNiagaraStackFunctionInput* FInputBindingContext::Resolve(UNiagaraSystem& System,
    FNiagaraEmitterHandle& Emitter, UNiagaraNodeFunctionCall& Module,
    const FNiagaraVariable& Variable, FInputBindingError& Error)
{
    UNiagaraStackEntry* Root = ResolveRoot(System, Emitter, Error);
    if (!Root)
    {
        return nullptr;
    }
    TArray<UNiagaraStackFunctionInput*> Matches;
    TArray<UNiagaraStackFunctionInput*> Inputs;
    TArray<FString> Candidates;
    Root->GetUnfilteredChildrenOfType(Inputs, true);
    for (UNiagaraStackFunctionInput* Input : Inputs)
    {
        if (&Input->GetInputFunctionCallNode() == &Module)
        {
            Candidates.Add(Input->GetInputParameterHandle().GetParameterHandleString().ToString()
                + TEXT(":") + Input->GetInputType().GetName());
        }
        if (&Input->GetInputFunctionCallNode() == &Module
            && Input->GetInputParameterHandle().GetParameterHandleString() == Variable.GetName()
            && Input->GetInputType() == Variable.GetType())
        {
            Matches.Add(Input);
        }
    }
    if (Matches.Num() != 1)
    {
        Error.Code = TEXT("stack_input_unresolved");
        Error.Message = FString::Printf(TEXT("input=%s type=%s native_stack_matches=%d candidates=%s"),
            *Variable.GetName().ToString(), *Variable.GetType().GetName(), Matches.Num(),
            *FString::Join(Candidates, TEXT(",")));
        return nullptr;
    }
    return Matches[0];
}
}
