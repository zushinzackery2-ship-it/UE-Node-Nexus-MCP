#include "Diagnostics/Runtime/NexusRuntimeDiagnostics.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "UObject/GarbageCollection.h"

namespace UeNodeNexusBridge::RuntimeDiagnostics
{
void RecordMessage(const FName& Source, const TSharedRef<FTokenizedMessage>& Message)
{
    const auto Severity = Message->GetSeverity();
    if (Severity > EMessageSeverity::Warning)
    {
        return;
    }
    auto Event = MakeShared<FJsonObject>();
    Event->SetStringField(TEXT("severity"), Severity <= EMessageSeverity::Error ? TEXT("error") : TEXT("warning"));
    Event->SetStringField(TEXT("source"), Source.ToString());
    Event->SetStringField(TEXT("code"), Source == TEXT("PIE") ? TEXT("pie_message") : TEXT("message_log"));
    Event->SetStringField(TEXT("message"), Message->ToText().ToString());
    if (IsInGameThread() && !IsGarbageCollecting())
    {
        for (const auto& Token : Message->GetMessageTokens())
        {
            if (Token->GetType() != EMessageToken::Object)
            {
                continue;
            }
            const auto ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
            const UObject* Object = ObjectToken->GetObject().Get();
            if (const auto* Node = Cast<UEdGraphNode>(Object))
            {
                Event->SetStringField(TEXT("node"), Token->ToText().ToString());
                Event->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
                Event->SetStringField(TEXT("node_path"), ObjectToken->GetOriginalObjectPathName());
            }
            if (const auto* Graph = Cast<UEdGraph>(Object))
            {
                Event->SetStringField(TEXT("graph"), Graph->GetName());
            }
            if (const auto* Function = Cast<UFunction>(Object))
            {
                Event->SetStringField(TEXT("function"), Function->GetName());
            }
            const auto* Blueprint = Cast<UBlueprint>(Object);
            if (!Blueprint && Object)
            {
                Blueprint = Object->GetTypedOuter<UBlueprint>();
            }
            if (Blueprint)
            {
                Event->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
            }
        }
    }
    Record(Event);
}
}
