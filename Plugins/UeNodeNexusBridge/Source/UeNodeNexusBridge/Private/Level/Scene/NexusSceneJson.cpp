#include "NexusSceneJson.h"
#include "NexusSceneIdentity.h"

#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UeNodeNexusBridgeTranscodeApi.h"

namespace UeNodeNexusBridge::Scene
{
FString String(const FObject& Json, const TCHAR* Key, const FString& Default)
{
    FString Value;
    return Json.IsValid() && Json->TryGetStringField(Key, Value) ? Value : Default;
}

FObject Object(const FObject& Json, const TCHAR* Key)
{
    const FObject* Value = nullptr;
    return Json.IsValid() && Json->TryGetObjectField(Key, Value) ? *Value : MakeShared<FJsonObject>();
}

const FRows& Rows(const FObject& Json, const TCHAR* Key)
{
    static const FRows Empty;
    const FRows* Value = nullptr;
    return Json.IsValid() && Json->TryGetArrayField(Key, Value) ? *Value : Empty;
}

FString TransformText(const FTransform& Value)
{
    FString Text;
    TBaseStructure<FTransform>::Get()->ExportText(Text, &Value, nullptr, nullptr, PPF_None, nullptr);
    return Text;
}

bool ReadTransform(const FString& Text, FTransform& Value, FString& Error)
{
    Value = FTransform::Identity;
    if (Text.IsEmpty())
    {
        return true;
    }
    const TCHAR* End = TBaseStructure<FTransform>::Get()->ImportText(*Text, &Value, nullptr, PPF_None, nullptr, TEXT("Transform"));
    if (!End || !FString(End).TrimStartAndEnd().IsEmpty() || Value.ContainsNaN() || !Value.GetRotation().IsNormalized())
    {
        Error = TEXT("invalid_transform: expected a finite UE Transform with a normalized quaternion");
        return false;
    }
    return true;
}

static FString Canonical(const TSharedPtr<FJsonValue>& Value)
{
    if (Value->Type == EJson::Object)
    {
        TArray<FString> Keys;
        Value->AsObject()->Values.GetKeys(Keys);
        Keys.Sort();
        FString Text = TEXT("{");
        for (const FString& Key : Keys)
        {
            const FString Encoded = Canonical(Value->AsObject()->Values[Key]);
            Text += FString::FromInt(Key.Len()) + TEXT(":") + Key + FString::FromInt(Encoded.Len()) + TEXT(":") + Encoded;
        }
        return Text + TEXT("}");
    }
    if (Value->Type == EJson::Array)
    {
        FString Text = TEXT("[");
        for (const auto& Item : Value->AsArray())
        {
            const FString Encoded = Canonical(Item);
            Text += FString::FromInt(Encoded.Len()) + TEXT(":") + Encoded;
        }
        return Text + TEXT("]");
    }
    FString Text;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
    FJsonSerializer::Serialize(Value, FString(), Writer);
    return Text;
}

FString Digest(const FObject& Json)
{
    return StableGuid(Canonical(MakeShared<FJsonValueObject>(Json))).ToString(EGuidFormats::Digits);
}

bool ReadFile(const FString& File, FObject& Json, FString& Error)
{
    FString Text;
    if (!Transcode::IsInsideMirrorRoot(File) || !FFileHelper::LoadFileToString(Text, *File)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
    {
        Error = TEXT("invalid_scene_file: readable JSON inside the mirror root is required");
        return false;
    }
    return true;
}
}
