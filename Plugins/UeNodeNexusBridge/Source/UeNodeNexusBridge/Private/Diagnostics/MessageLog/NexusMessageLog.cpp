#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"

#include "Logging/IMessageLog.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "Modules/ModuleManager.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
void RecordMessage(const FName& Source, const TSharedRef<FTokenizedMessage>& Message);

namespace
{
FMessageLog::FGetLog Previous;
FDelegateHandle Owner;
TAtomic<bool> bEnabled(false);

class FObservedLog final : public IMessageLog
{
    FName Source;
    TSharedRef<IMessageLog> Inner;
public:
    FObservedLog(FName Name, TSharedRef<IMessageLog> Target) : Source(Name), Inner(Target)
    {
    }
    virtual void AddMessage(const TSharedRef<FTokenizedMessage>& Message, bool bMirror = true) override
    {
        if (bEnabled)
        {
            RecordMessage(Source, Message);
        }
        Inner->AddMessage(Message, bMirror);
    }
    virtual void AddMessages(const TArray<TSharedRef<FTokenizedMessage>>& Messages, bool bMirror = true) override
    {
        if (bEnabled)
        {
            for (const auto& Message : Messages)
            {
                RecordMessage(Source, Message);
            }
        }
        Inner->AddMessages(Messages, bMirror);
    }
    virtual void NewPage(const FText& Title) override
    {
        Inner->NewPage(Title);
    }
    virtual void SetCurrentPage(const FText& Title) override
    {
        Inner->SetCurrentPage(Title);
    }
    virtual void SetCurrentPage(const uint32 Index) override
    {
        Inner->SetCurrentPage(Index);
    }
    virtual void NotifyIfAnyMessages(const FText& Text, EMessageSeverity::Type Severity, bool bForce = false) override
    {
        Inner->NotifyIfAnyMessages(Text, Severity, bForce);
    }
    virtual void Open() override
    {
        Inner->Open();
    }
    virtual int32 NumMessages(EMessageSeverity::Type Severity) override
    {
        return Inner->NumMessages(Severity);
    }
};

TSharedRef<IMessageLog> Observe(const FName& Name)
{
    return MakeShared<FObservedLog>(Name, Previous.Execute(Name));
}
}

void StartMessages()
{
    FModuleManager::LoadModuleChecked<FMessageLogModule>(TEXT("MessageLog"));
    Previous = FMessageLog::OnGetLog();
    check(Previous.IsBound());
    bEnabled = true;
    FMessageLog::OnGetLog().BindStatic(&Observe);
    Owner = FMessageLog::OnGetLog().GetHandle();
}

void StopMessages()
{
    bEnabled = false;
    if (FMessageLog::OnGetLog().GetHandle() == Owner)
    {
        FMessageLog::OnGetLog() = Previous;
    }
}
}
