#include "NexusSchema.h"

#include "Transcode/Blueprint/UeNodeNexusBridgeTranscodeBlueprintShared.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_Tunnel.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge::Transcode
{
namespace
{
void AppendTunnelPins(UEdGraph* Graph, TArray<TSharedPtr<FJsonValue>>& Pins)
{
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(Node);
        // A macro's interface is the exact tunnel pair; composites and nested
        // macro instances carry tunnels of their own that no caller sees.
        if (Tunnel == nullptr || Tunnel->GetClass() != UK2Node_Tunnel::StaticClass())
        {
            continue;
        }
        for (UEdGraphPin* Pin : Tunnel->Pins)
        {
            // Expanded sub-pins describe a struct inside one pin, not a pin of the macro.
            if (Pin == nullptr || Pin->ParentPin != nullptr)
            {
                continue;
            }
            // An instance mirrors its tunnel: the entry tunnel's output is the
            // caller's input, the exit tunnel's input is the caller's output.
            const bool bInput = Pin->Direction == EGPD_Output;
            const TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("name"), Pin->PinName.ToString());
            Row->SetStringField(TEXT("dir"), bInput ? TEXT("in") : TEXT("out"));
            Row->SetStringField(TEXT("default"), Pin->GetDefaultAsString());
            Row->SetObjectField(TEXT("type"), PinTypeJson(Pin->PinType));
            Pins.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
}

void AddLibrary(UBlueprint* Blueprint, const TSharedPtr<FJsonObject>& Macros, TSet<FString>& Seen)
{
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (Graph == nullptr)
        {
            continue;
        }
        TArray<TSharedPtr<FJsonValue>> Pins;
        AppendTunnelPins(Graph, Pins);
        if (Pins.Num() == 0)
        {
            continue;
        }
        const FString Key = Blueprint->GetPathName() + TEXT(".") + Graph->GetName();
        if (Seen.Contains(Key))
        {
            continue;
        }
        Seen.Add(Key);
        const TSharedPtr<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("path"), Key);
        Record->SetStringField(TEXT("owner"), Blueprint->GetPathName());
        Record->SetStringField(TEXT("name"), Graph->GetName());
        Record->SetStringField(TEXT("coverage"), TEXT("reflected"));
        Record->SetArrayField(TEXT("pins"), Pins);
        Macros->SetObjectField(Key, Record);
    }
}
}

TSharedPtr<FJsonObject> MacroIndex()
{
    const TSharedPtr<FJsonObject> Macros = MakeShared<FJsonObject>();
    TSet<FString> Seen;
    // The engine library backs the loops and gates every Blueprint author uses;
    // it is loaded explicitly so lint can name their tunnel pins instead of
    // accepting any spelling an author guesses. The path is the one macro
    // creation resolves a bare ``StandardMacros`` owner to.
    if (UBlueprint* Standard = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"),
                                                     nullptr, LOAD_NoWarn | LOAD_Quiet))
    {
        AddLibrary(Standard, Macros, Seen);
    }
    // Project macro libraries are exported only while the editor holds them;
    // an unloaded library stays unknown to lint, which then checks no pin.
    for (TObjectIterator<UBlueprint> Blueprint; Blueprint; ++Blueprint)
    {
        if (Blueprint->BlueprintType != BPTYPE_MacroLibrary
            || Blueprint->HasAnyFlags(RF_Transient)
            || Blueprint->GetName().StartsWith(TEXT("SKEL_"))
            || Blueprint->GetName().StartsWith(TEXT("REINST_")))
        {
            continue;
        }
        AddLibrary(*Blueprint, Macros, Seen);
    }
    return Macros;
}
}
