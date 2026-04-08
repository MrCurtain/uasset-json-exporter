#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "DataAssetExportCommandlet.generated.h"

/*
 * Exports DataAsset properties to JSON via UObject reflection.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=DataAssetExport -assets="/Game/Path/DA_A,/Game/Path/DA_B"
 *
 * Output:
 *   <ProjectDir>/Intermediate/UAssetExport/<AssetPath>.json
 */
UCLASS()
class UDataAssetExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UDataAssetExportCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    TSharedPtr<FJsonObject> ExportDataAsset(class UDataAsset* DataAsset) const;
    TSharedPtr<FJsonObject> ExportProperties(UObject* Object, UClass* StopAtClass) const;

    TArray<FString> ParseAssetPaths(const FString& Params) const;
    FString GetExportPath(const FString& AssetPath) const;
    bool SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const;
};
