#include "NexusCompileFeedback.h"

namespace UeNodeNexusBridge
{
FCompileFeedback::FCompileFeedback(bool bEnabled)
    : Previous(GWarn), bInstalled(bEnabled), bPreviousSlowTask(GIsSlowTask)
{
    check(IsInGameThread());
    if (bInstalled)
    {
        GWarn = this;
    }
}

FCompileFeedback::~FCompileFeedback()
{
    if (bInstalled)
    {
        check(GWarn == this && GetScopeStack().Num() == 0);
        GWarn = Previous;
        GIsSlowTask = bPreviousSlowTask;
    }
}

void FCompileFeedback::Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category)
{
    Previous->Serialize(Text, Verbosity, Category);
}

void FCompileFeedback::Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category, double Time)
{
    Previous->Serialize(Text, Verbosity, Category, Time);
}

void FCompileFeedback::SerializeRecord(const UE::FLogRecord& Record)
{
    Previous->SerializeRecord(Record);
}
}
