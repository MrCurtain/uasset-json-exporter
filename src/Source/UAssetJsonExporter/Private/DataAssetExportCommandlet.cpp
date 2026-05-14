#include "DataAssetExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterUtil.h"
#include "UAssetJsonExporterVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UDataAssetExportCommandlet::UDataAssetExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UDataAssetExportCommandlet::Main(const FString& Params)
{
    if (UAssetJsonExporter::AbortIfLiveEditor())
    {
        return 2;
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - DataAssetExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = UAssetJsonExporter::ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/DA_A,/Game/Path/DA_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        UDataAsset* DataAsset = LoadObject<UDataAsset>(nullptr, *AssetPath);
        if (!DataAsset)
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load DataAsset: %s"), *AssetPath);
            continue;
        }

        TSharedPtr<FJsonObject> JsonObject = ExportDataAsset(DataAsset);
        if (!JsonObject.IsValid())
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to export DataAsset: %s"), *AssetPath);
            continue;
        }

        FString OutputPath = UAssetJsonExporter::GetExportPath(AssetPath);
        if (UAssetJsonExporter::SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
        {
            UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
            ExportedCount++;
        }
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d data assets exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UDataAssetExportCommandlet::ExportDataAsset(UDataAsset* DataAsset) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("DataAsset"));
    Root->SetStringField(TEXT("DataAssetName"), DataAsset->GetName());
    Root->SetStringField(TEXT("AssetPath"), DataAsset->GetPathName());
    Root->SetStringField(TEXT("Class"), DataAsset->GetClass()->GetName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    // Class hierarchy
    TArray<TSharedPtr<FJsonValue>> HierarchyArray;
    UClass* CurrentClass = DataAsset->GetClass();
    while (CurrentClass && CurrentClass != UObject::StaticClass())
    {
        HierarchyArray.Add(MakeShared<FJsonValueString>(CurrentClass->GetName()));
        CurrentClass = CurrentClass->GetSuperClass();
    }
    Root->SetArrayField(TEXT("ClassHierarchy"), HierarchyArray);

    // All properties from the DataAsset subclass down to (but not including) UDataAsset base
    TSharedPtr<FJsonObject> Props = ExportSubclassProperties(DataAsset, UDataAsset::StaticClass());
    if (Props.IsValid() && Props->Values.Num() > 0)
    {
        Root->SetObjectField(TEXT("Properties"), Props);
    }

    return Root;
}

TSharedPtr<FJsonObject> UDataAssetExportCommandlet::ExportSubclassProperties(UObject* Object, UClass* StopAtClass) const
{
    TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

    UClass* CurrentClass = Object->GetClass();
    while (CurrentClass && CurrentClass != StopAtClass)
    {
        for (TFieldIterator<FProperty> PropIt(CurrentClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
        {
            FProperty* Prop = *PropIt;
            if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
            {
                continue;
            }

            // Handle array properties with element detail
            if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
            {
                FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Object));
                int32 Num = ArrayHelper.Num();

                TArray<TSharedPtr<FJsonValue>> ElementsArray;
                FProperty* InnerProp = ArrayProp->Inner;

                // If inner is struct or object, export each element individually
                if (CastField<FStructProperty>(InnerProp) || CastField<FObjectProperty>(InnerProp))
                {
                    for (int32 i = 0; i < Num; i++)
                    {
                        FString ElemValue;
                        InnerProp->ExportTextItem_Direct(ElemValue, ArrayHelper.GetRawPtr(i), nullptr, Object, PPF_None);
                        ElementsArray.Add(MakeShared<FJsonValueString>(ElemValue));
                    }
                    // Stable key (no count suffix) so external consumers can index by property name.
                    TSharedPtr<FJsonObject> ArrayInfo = MakeShared<FJsonObject>();
                    ArrayInfo->SetNumberField(TEXT("Count"), Num);
                    ArrayInfo->SetArrayField(TEXT("Elements"), ElementsArray);
                    Props->SetObjectField(Prop->GetName(), ArrayInfo);
                }
                else
                {
                    FString Value;
                    Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
                    if (!Value.IsEmpty())
                    {
                        Props->SetStringField(Prop->GetName(), Value);
                    }
                }
            }
            else
            {
                FString Value;
                Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
                if (!Value.IsEmpty())
                {
                    Props->SetStringField(Prop->GetName(), Value);
                }
            }
        }
        CurrentClass = CurrentClass->GetSuperClass();
    }

    return Props;
}

