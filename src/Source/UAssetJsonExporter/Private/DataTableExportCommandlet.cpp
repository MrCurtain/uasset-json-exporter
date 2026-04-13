#include "DataTableExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UDataTableExportCommandlet::UDataTableExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UDataTableExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - DataTableExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/DT_A,/Game/Path/DT_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *AssetPath);
        if (!DataTable)
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load DataTable: %s"), *AssetPath);
            continue;
        }

        TSharedPtr<FJsonObject> JsonObject = ExportDataTable(DataTable);
        if (!JsonObject.IsValid())
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to export DataTable: %s"), *AssetPath);
            continue;
        }

        FString OutputPath = GetExportPath(AssetPath);
        if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
        {
            UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
            ExportedCount++;
        }
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d data tables exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UDataTableExportCommandlet::ExportDataTable(UDataTable* DataTable) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("DataTable"));
    Root->SetStringField(TEXT("DataTableName"), DataTable->GetName());
    Root->SetStringField(TEXT("AssetPath"), DataTable->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    const UScriptStruct* RowStruct = DataTable->GetRowStruct();
    if (!RowStruct)
    {
        UE_LOG(LogUAssetJsonExporter, Warning, TEXT("DataTable has no row struct: %s"), *DataTable->GetName());
        return Root;
    }

    Root->SetStringField(TEXT("RowStruct"), RowStruct->GetName());
    Root->SetNumberField(TEXT("RowCount"), DataTable->GetRowMap().Num());

    TSharedPtr<FJsonObject> RowsObject = MakeShared<FJsonObject>();

    for (const auto& Pair : DataTable->GetRowMap())
    {
        FString RowName = Pair.Key.ToString();
        const void* RowData = Pair.Value;

        TSharedPtr<FJsonObject> RowObj = ExportRow(RowStruct, RowData);
        if (RowObj.IsValid())
        {
            RowsObject->SetObjectField(RowName, RowObj);
        }
    }

    Root->SetObjectField(TEXT("Rows"), RowsObject);

    return Root;
}

TSharedPtr<FJsonObject> UDataTableExportCommandlet::ExportRow(const UScriptStruct* RowStruct, const void* RowData) const
{
    TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();

    for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
    {
        FProperty* Prop = *PropIt;

        FString Value;
        const void* PropertyValue = Prop->ContainerPtrToValuePtr<void>(RowData);
        Prop->ExportTextItem_Direct(Value, PropertyValue, nullptr, nullptr, PPF_None);

        if (!Value.IsEmpty())
        {
            RowObj->SetStringField(Prop->GetName(), Value);
        }
    }

    return RowObj;
}

TArray<FString> UDataTableExportCommandlet::ParseAssetPaths(const FString& Params) const
{
    TArray<FString> Result;

    FString AssetsValue;
    if (FParse::Value(*Params, TEXT("-assets="), AssetsValue, false))
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

FString UDataTableExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UDataTableExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
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
