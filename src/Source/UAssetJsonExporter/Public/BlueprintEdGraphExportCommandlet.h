#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "BlueprintEdGraphExportCommandlet.generated.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

/*
 * Exports Blueprint graph structures (nodes, pins, connections) to JSON
 * for external tooling consumption.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=BlueprintEdGraphExport -assets="/Game/Path/BP_A,/Game/Path/BP_B"
 *
 * Output:
 *   <ProjectDir>/Intermediate/UAssetExport/<AssetPath>.json
 */
UCLASS()
class UBlueprintEdGraphExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UBlueprintEdGraphExportCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    TSharedPtr<FJsonObject> ExportBlueprint(class UBlueprint* Blueprint) const;
    TSharedPtr<FJsonObject> ExportGraph(const UEdGraph* Graph) const;
    TSharedPtr<FJsonObject> ExportNode(const UEdGraphNode* Node) const;
    TSharedPtr<FJsonObject> ExportPin(const UEdGraphPin* Pin) const;

    // Compare Instance properties against its class CDO, output differences
    void ExportPropertyOverrides(UObject* Instance, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

    // Compare Instance properties against an explicit Reference object, output differences
    void ExportPropertyOverridesCompare(UObject* Instance, UObject* Reference, TArray<TSharedPtr<FJsonValue>>& OutArray) const;

    TArray<FString> ParseAssetPaths(const FString& Params) const;
    FString GetExportPath(const FString& AssetPath) const;
    bool SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const;
};
