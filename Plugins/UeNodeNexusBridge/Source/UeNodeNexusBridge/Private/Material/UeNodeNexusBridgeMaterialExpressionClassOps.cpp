#include "UeNodeNexusBridgeOperations.h"

#include "Dom/JsonValue.h"
#include "Materials/MaterialExpression.h"
#include "UeNodeNexusBridgeJson.h"
#include "UeNodeNexusBridgeMaterialPropertySchema.h"
#include "UObject/UObjectIterator.h"

namespace UeNodeNexusBridge
{
static FString ShortMaterialExpressionClassName(UClass* Class)
{
    FString Name = Class ? Class->GetName() : FString();
    Name.RemoveFromStart(TEXT("MaterialExpression"));
    return Name;
}

static bool ShouldIncludeMaterialExpressionClass(UClass* Class, bool bIncludeAbstract, bool bIncludeDeprecated)
{
    if (Class == nullptr || Class == UMaterialExpression::StaticClass() || !Class->IsChildOf(UMaterialExpression::StaticClass()))
    {
        return false;
    }
    if (!bIncludeAbstract && Class->HasAnyClassFlags(CLASS_Abstract))
    {
        return false;
    }
    if (!bIncludeDeprecated && Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
    {
        return false;
    }
    return true;
}

static TArray<UClass*> CollectMaterialExpressionClasses(bool bIncludeAbstract, bool bIncludeDeprecated)
{
    TArray<UClass*> Classes;
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        if (ShouldIncludeMaterialExpressionClass(Class, bIncludeAbstract, bIncludeDeprecated))
        {
            Classes.Add(Class);
        }
    }

    Classes.Sort(
        [](const UClass& Left, const UClass& Right)
        {
            return Left.GetPathName() < Right.GetPathName();
        });
    return Classes;
}

static FString BuildMaterialExpressionClassesText(const TArray<UClass*>& Classes, int32 Start, int32 End)
{
    FString Text;
    for (int32 Index = Start; Index < End; ++Index)
    {
        UClass* Class = Classes[Index];
        const TArray<TSharedPtr<FJsonValue>> Params = BuildMaterialExpressionClassParams(Class);
        Text += FString::Printf(
            TEXT("class_%03d.%s -> %s params=%d\n"),
            Index,
            *ShortMaterialExpressionClassName(Class),
            *Class->GetPathName(),
            Params.Num());
    }
    return Text;
}

TSharedPtr<FJsonObject> HandleMaterialExpressionClassesList(const FString& Operation, const FString& RequestId, const TSharedPtr<FJsonObject>& Payload)
{
    bool bIncludeAbstract = false;
    bool bIncludeDeprecated = false;
    bool bIncludeParams = false;
    FString Format = TEXT("compact");
    Payload->TryGetBoolField(TEXT("include_abstract"), bIncludeAbstract);
    Payload->TryGetBoolField(TEXT("include_deprecated"), bIncludeDeprecated);
    Payload->TryGetBoolField(TEXT("include_params"), bIncludeParams);
    Payload->TryGetStringField(TEXT("format"), Format);
    const bool bFull = Format.Equals(TEXT("full"), ESearchCase::IgnoreCase);
    const bool bIndexed = Format.Equals(TEXT("indexed"), ESearchCase::IgnoreCase);

    const TArray<UClass*> Classes = CollectMaterialExpressionClasses(bIncludeAbstract, bIncludeDeprecated);
    const int32 Cursor = ReadCursor(Payload);
    const int32 Limit = ReadLimit(Payload, 500, 2000);
    const int32 Start = FMath::Clamp(Cursor, 0, Classes.Num());
    const int32 End = FMath::Min(Start + Limit, Classes.Num());

    TArray<TSharedPtr<FJsonValue>> Items;
    if (!bIndexed)
    {
        for (int32 Index = Start; Index < End; ++Index)
        {
            UClass* Class = Classes[Index];
            const TArray<TSharedPtr<FJsonValue>> Params = BuildMaterialExpressionClassParams(Class);

            TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetNumberField(TEXT("index"), Index);
            Item->SetStringField(TEXT("name"), Class->GetName());
            Item->SetStringField(TEXT("short_name"), ShortMaterialExpressionClassName(Class));
            Item->SetStringField(TEXT("path"), Class->GetPathName());
            Item->SetBoolField(TEXT("abstract"), Class->HasAnyClassFlags(CLASS_Abstract));
            Item->SetBoolField(TEXT("deprecated"), Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists));
            Item->SetNumberField(TEXT("param_count"), Params.Num());
            if (bIncludeParams || bFull)
            {
                Item->SetArrayField(TEXT("params"), Params);
            }
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
    }

    TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
    Data->SetStringField(TEXT("format"), bIndexed ? TEXT("material_expression_classes_indexed") : TEXT("material_expression_classes"));
    Data->SetNumberField(TEXT("total"), Classes.Num());
    Data->SetNumberField(TEXT("count"), End - Start);
    Data->SetStringField(TEXT("cursor"), FString::FromInt(Start));
    Data->SetStringField(TEXT("next_cursor"), End < Classes.Num() ? FString::FromInt(End) : FString());
    if (!bIndexed)
    {
        Data->SetArrayField(TEXT("classes"), Items);
    }
    SetTextPayload(Data, BuildMaterialExpressionClassesText(Classes, Start, End));

    TSharedPtr<FJsonObject> Response = MakeEnvelope(Operation, RequestId, true);
    Response->SetObjectField(TEXT("data"), Data);
    return Response;
}
}
