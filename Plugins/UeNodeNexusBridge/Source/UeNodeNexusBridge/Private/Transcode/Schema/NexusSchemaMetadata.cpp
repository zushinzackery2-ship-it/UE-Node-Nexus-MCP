#include "NexusSchema.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "K2Node.h"
#include "EdGraph/EdGraphNode.h"
#include "Level/Scene/NexusSceneWorld.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeTranscode.h"
#include "UeNodeNexusBridgeTranscodeBlueprintShared.h"
#include "UObject/UnrealType.h"

namespace UeNodeNexusBridge::Transcode
{
void AddClassMetadata(UClass* Class, const TSharedPtr<FJsonObject>& Record)
{
    Record->SetStringField(TEXT("path"), Class->GetPathName());
    Record->SetStringField(TEXT("module"), Class->GetOutermost()->GetName().Replace(TEXT("/Script/"), TEXT("")));
    Record->SetStringField(TEXT("tooltip"), Class->GetToolTipText().ToString());
    Record->SetStringField(TEXT("inheritance"), Class->GetSuperClass() ? Class->GetSuperClass()->GetPathName() : FString());
    Record->SetBoolField(TEXT("engine_available"), true);
    Record->SetBoolField(TEXT("deprecated"), Class->HasAnyClassFlags(CLASS_Deprecated));
    const bool bK2 = Class->IsChildOf(UEdGraphNode::StaticClass());
    const bool bNode = Class->IsChildOf(UMaterialExpression::StaticClass());
    const bool bComponent = Class->IsChildOf(UActorComponent::StaticClass());
    const bool bActor = Class->IsChildOf(AActor::StaticClass()) && Scene::SupportedClass(Class);
    bool bRenderer = false;
    for (UClass* Base = Class; Base; Base = Base->GetSuperClass())
    {
        bRenderer |= Base->GetName() == TEXT("NiagaraRendererProperties");
    }
    const FString Kind = KindForClass(Class);
    const bool bSupported = bK2 ? IsSupportedNodeClass(Class) : bNode || bComponent || bActor || bRenderer
        || (Kind != TEXT("stub") && Kind != TEXT("niagara_emitter"));
    const auto Bridge = MakeShared<FJsonObject>();
    Bridge->SetBoolField(TEXT("inspect"), true);
    Bridge->SetBoolField(TEXT("create"), bSupported && Kind != TEXT("niagara_system"));
    Bridge->SetBoolField(TEXT("write"), bSupported);
    Bridge->SetBoolField(TEXT("delete"), bSupported);
    Bridge->SetStringField(TEXT("source"), bK2 ? TEXT("Transcode::IsSupportedNodeClass") : TEXT("Transcode kind and entity adapters"));
    Bridge->SetStringField(TEXT("entry"), bActor || bComponent ? TEXT("scene_apply/transcode_apply") : Kind == TEXT("niagara_system") ? TEXT("vfx_transcode_apply") : TEXT("transcode_apply"));
    if (bK2)
    {
        Bridge->SetStringField(TEXT("context"), TEXT("valid Blueprint graph; dynamic pins require target/function context"));
    }
    Record->SetObjectField(TEXT("bridge"), Bridge);
    const auto Conditions = MakeShared<FJsonObject>();
    for (const TCHAR* Key : { TEXT("BlueprintInternalUseOnly"), TEXT("RestrictedToClasses"), TEXT("ShowWorldContextPin"), TEXT("DisplayName"), TEXT("Category") })
    {
        if (Class->HasMetaData(Key))
        {
            Conditions->SetStringField(Key, Class->GetMetaData(Key));
        }
    }
    Record->SetObjectField(TEXT("conditions"), Conditions);
    Record->SetStringField(TEXT("plugin"), TEXT("Engine/Project"));
    const FString ModuleName = Class->GetOutermost()->GetName().Replace(TEXT("/Script/"), TEXT(""));
    for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
    {
        for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
        {
            if (Module.Name.ToString() == ModuleName)
            {
                Record->SetStringField(TEXT("plugin"), Plugin->GetName());
            }
        }
    }
}

void AddPropertyMetadata(FProperty* Property, const TSharedPtr<FJsonObject>& Record)
{
    Record->SetStringField(TEXT("tooltip"), Property->GetToolTipText().ToString());
    Record->SetStringField(TEXT("default_source"), Record->HasField(TEXT("default")) ? TEXT("class_default_object") : TEXT("metadata_not_provided"));
    Record->SetBoolField(TEXT("readable"), true);
    Record->SetBoolField(TEXT("writable"), IsEditableProperty(Property));
    Record->SetBoolField(TEXT("nullable"), !Property->HasAnyPropertyFlags(CPF_NoClear));
    Record->SetStringField(TEXT("declared_by"), Property->GetOwnerStruct()->GetPathName());
    Record->SetBoolField(TEXT("deprecated"), Property->HasAnyPropertyFlags(CPF_Deprecated));
    const TPair<const TCHAR*, const TCHAR*> Metadata[] =
    {
        { TEXT("UIMin"), TEXT("ui_min") }, { TEXT("UIMax"), TEXT("ui_max") },
        { TEXT("Units"), TEXT("units") }, { TEXT("EditCondition"), TEXT("conditions") },
        { TEXT("Category"), TEXT("category") }, { TEXT("DisplayName"), TEXT("display_name") },
        { TEXT("DeprecationMessage"), TEXT("deprecation_message") }
    };
    for (const auto& Pair : Metadata)
    {
        if (Property->HasMetaData(Pair.Key))
        {
            Record->SetStringField(Pair.Value, Property->GetMetaData(Pair.Key));
        }
    }
}
}
