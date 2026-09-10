#include "NexusSceneApply.h"

#include "NexusSceneIdentity.h"
#include "Diagnostics/Compilation/UeNodeNexusBridgeCompilation.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"
#include "UeNodeNexusBridgeRequestDispatch.h"

namespace UeNodeNexusBridge::Scene
{
static void ApplyOperations(FApply& Context)
{
    for (const auto& Value : Rows(Context.Plan, TEXT("ops")))
    {
        const FObject Op = Value->AsObject();
        const FString Verb = String(Op, TEXT("op"));
        const bool bOk = Verb.EndsWith(TEXT("_actor")) ? ApplyActor(Context, Op) : ApplyComponent(Context, Op);
        FObject Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("op"), Verb);
        Result->SetStringField(TEXT("id"), String(Op, TEXT("id")));
        Result->SetBoolField(TEXT("ok"), bOk);
        if (!bOk)
        {
            Result->SetStringField(TEXT("error"), Context.Error);
        }
        Context.Results.Add(MakeShared<FJsonValueObject>(Result));
        if (!bOk)
        {
            break;
        }
    }
}

static FObject ReadAfter(FApply& Context, FString& Error)
{
    FObject Selector = MakeShared<FJsonObject>();
    Selector->SetStringField(TEXT("name"), String(Context.Plan, TEXT("name")));
    FRows Refs;
    for (const auto& Pair : Context.Actors)
    {
        if (!IsValid(Pair.Value))
        {
            continue;
        }
        FObject Ref = MakeShared<FJsonObject>();
        Ref->SetStringField(TEXT("id"), Pair.Key);
        Ref->SetStringField(TEXT("level_path"), Pair.Value->GetLevel()->GetOutermost()->GetName());
        Refs.Add(MakeShared<FJsonValueObject>(Ref));
    }
    Selector->SetArrayField(TEXT("actors"), Refs);
    return ExportScene(Context.World, Selector, Error);
}

FObject ApplyScene(UWorld* World, const FObject& Plan, bool bDryRun, bool bSave, FString& Error)
{
    const double Started = FPlatformTime::Seconds();
    const FObject Selector = Object(Plan, TEXT("selector"));
    if (String(Plan, TEXT("name")).IsEmpty() || String(Plan, TEXT("name")) != String(Selector, TEXT("name")))
    {
        Error = TEXT("scene_identity_mismatch: plan and selector must name the same group");
        return nullptr;
    }
    const FObject Before = ExportScene(World, Selector, Error);
    if (!Before.IsValid())
    {
        return nullptr;
    }
    if (!Rows(Before, TEXT("unavailable")).IsEmpty() || String(Before, TEXT("revision")) != String(Plan, TEXT("expected_revision")))
    {
        Error = TEXT("scene_conflict: target changed or contains unloaded actors");
        return nullptr;
    }
    FApply Context;
    Context.World = World;
    Context.Plan = Plan;
    Context.Before = Before;
    if (!Preflight(Context))
    {
        Error = Context.Error;
        return nullptr;
    }
    FObject Data = MakeShared<FJsonObject>();
    Data->SetBoolField(TEXT("dry_run"), bDryRun);
    Data->SetNumberField(TEXT("planned"), Rows(Plan, TEXT("ops")).Num());
    if (bDryRun)
    {
        Data->SetBoolField(TEXT("ok"), true);
        return Data;
    }
    const bool bHasOperations = !Rows(Plan, TEXT("ops")).IsEmpty();
    bool bWasDirty = false;
    Before->TryGetBoolField(TEXT("dirty"), bWasDirty);
    if (bHasOperations && !PrepareResources(Context))
    {
        Error = Context.Error;
        return nullptr;
    }
    {
        FScopedTransaction Transaction(FText::FromString(TEXT("Nexus scene: ") + String(Plan, TEXT("name"))));
        if (BindScene(Context))
        {
            ApplyOperations(Context);
            if (bHasOperations && Context.Error.IsEmpty())
            {
                BindAppliedIdentities(Context);
            }
        }
    }
    const bool bNeedsResources = bHasOperations || Context.bChanged || bWasDirty;
    const bool bReady = Context.Error.IsEmpty() && (!bNeedsResources || PrepareResources(Context, false));
    FRows Saved, Failed;
    const bool bSaved = bReady && (!bSave || SaveScene(Context, Saved, Failed));
    if (!Failed.IsEmpty() && Context.Error.IsEmpty())
    {
        Context.Error = TEXT("scene_save_incomplete: inspect failed_packages");
    }
    int32 Applied = 0;
    for (const auto& Result : Context.Results)
    {
        bool bOk = false;
        Result->AsObject()->TryGetBoolField(TEXT("ok"), bOk);
        Applied += bOk ? 1 : 0;
    }
    Data->SetBoolField(TEXT("ok"), bReady && bSaved);
    Data->SetBoolField(TEXT("changed"), Context.bChanged);
    Data->SetBoolField(TEXT("saved"), bSave && bSaved);
    Data->SetNumberField(TEXT("applied"), Applied);
    Data->SetArrayField(TEXT("saved_packages"), Saved);
    Data->SetArrayField(TEXT("failed_packages"), Failed);
    Data->SetArrayField(TEXT("results"), Context.Results);
    Data->SetStringField(TEXT("error"), Context.Error);
    FString ExportError;
    const FObject After = ReadAfter(Context, ExportError);
    if (After.IsValid())
    {
        After->SetStringField(TEXT("request_token"), String(Plan, TEXT("request_token")));
        Data->SetObjectField(TEXT("snapshot"), After);
        Data->SetStringField(TEXT("revision"), String(After, TEXT("revision")));
        Data->SetStringField(TEXT("request_token"), String(Plan, TEXT("request_token")));
    }
    else
    {
        Data->SetBoolField(TEXT("ok"), false);
        Data->SetStringField(TEXT("export_error"), ExportError);
    }
    Data->SetNumberField(TEXT("duration_ms"), (FPlatformTime::Seconds() - Started) * 1000.0);
    UE_LOG(LogTemp, Display, TEXT("Nexus request=%s scene=%s phase=complete applied=%d saved=%d failed=%d duration_ms=%.3f"),
        *ActiveBridgeRequestId(), *String(Plan, TEXT("name")), Applied, Saved.Num(), Failed.Num(),
        (FPlatformTime::Seconds() - Started) * 1000.0);
    return Data;
}
}
