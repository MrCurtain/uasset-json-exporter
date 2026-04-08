#include "DataAssetExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
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
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - DataAssetExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

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

        FString OutputPath = GetExportPath(AssetPath);
        if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
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
    TSharedPtr<FJsonObject> Props = ExportProperties(DataAsset, UDataAsset::StaticClass());
    if (Props.IsValid() && Props->Values.Num() > 0)
    {
        Root->SetObjectField(TEXT("Properties"), Props);
    }

    return Root;
}

TSharedPtr<FJsonObject> UDataAssetExportCommandlet::ExportProperties(UObject* Object, UClass* StopAtClass) const
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
                    Props->SetArrayField(FString::Printf(TEXT("%s (%d)"), *Prop->GetName(), Num), ElementsArray);
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

TArray<FString> UDataAssetExportCommandlet::ParseAssetPaths(const FString& Params) const
{
    TArray<FString> Result;

    FString AssetsValue;
    if (FParse::Value(*Params, TEXT("-assets="), AssetsValue))
    {
        AssetsValue.TrimQuotesInline();
        AssetsValue.ParseIntoArray(Result, TEXT(","), true);

        for (FString& Path : Result)
        {
            Path.TrimStartAndEndInline();
        }
    }

    return Result;
}

FString UDataAssetExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UDataAssetExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
{
    FString OutputDir = FPaths::GetPath(FilePath);
    if (!IFileManager::Get().DirectoryExists(*OutputDir))
    {
        IFileManager::Get().MakeDirectory(*OutputDir, true);
    }

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (!FJsonSerializer::Serialize(JsonObject, Writer))
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("Failed to serialize JSON for: %s"), *FilePath);
        return false;
    }

    if (!FFileHelper::SaveStringToFile(OutputString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("Failed to write file: %s"), *FilePath);
        return false;
    }

    return true;
}
