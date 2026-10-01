#pragma once

#include "Misc/FeedbackContext.h"

namespace UeNodeNexusBridge
{
// Shader waits report progress through GWarn. The editor feedback context ticks
// Slate and Python delegates while that resource is still on the compile stack.
// Use an independent progress stack and forward diagnostics, without UI ticks.
class FCompileFeedback final : public FFeedbackContext
{
public:
    explicit FCompileFeedback(bool bEnabled);
    ~FCompileFeedback();
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override;
    virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override;
    virtual void SerializeRecord(const UE::FLogRecord& Record) override;

private:
    FFeedbackContext* Previous;
    bool bInstalled;
    bool bPreviousSlowTask;
};
}
