#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "AnimBlueprintExportCommandlet.generated.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UAnimationStateMachineGraph;

/*
 * Exports AnimBlueprint structure (EdGraph, StateMachines with states/transitions) to JSON.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=AnimBlueprintExport -assets="/Game/Path/ABP_A,/Game/Path/ABP_B"
 *
 * Output:
 *   <ProjectDir>/Intermediate/UAssetExport/<AssetPath>.json
 */
UCLASS()
class UAnimBlueprintExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UAnimBlueprintExportCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    TSharedPtr<FJsonObject> ExportAnimBlueprint(class UAnimBlueprint* AnimBP) const;
    TSharedPtr<FJsonObject> ExportStateMachine(UAnimationStateMachineGraph* SMGraph) const;

    // EdGraph export
    TSharedPtr<FJsonObject> ExportGraph(const UEdGraph* Graph) const;
    TSharedPtr<FJsonObject> ExportNode(const UEdGraphNode* Node) const;
    TSharedPtr<FJsonObject> ExportPin(const UEdGraphPin* Pin) const;

    TArray<FString> ParseAssetPaths(const FString& Params) const;
    FString GetExportPath(const FString& AssetPath) const;
    bool SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const;
};
