#include "NexusNiagaraCheckpoint.h"

#include "NiagaraParameterStore.h"
#include "NiagaraSystem.h"

namespace UeNodeNexusBridge::NiagaraCheckpoint
{
bool Prepare(UObject* Asset, FString& Error)
{
    auto* System = Cast<UNiagaraSystem>(Asset);
    if (!System)
    {
        Error = TEXT("Niagara system checkpoint has a different asset class");
        return false;
    }
    // UNiagaraSystem::PostLoad initializes null interface slots. Apply the same
    // engine operation before saving so native reload reproduces this baseline.
    // The original precondition observation remains in receipt.before.
    System->GetExposedParameters().SanityCheckData(true);
    return true;
}
}
