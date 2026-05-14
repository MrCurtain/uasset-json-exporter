#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "NiagaraSystemExportCommandlet.generated.h"

class UNiagaraSystem;

/*
 * Exports Niagara System structure (emitters, module stacks, parameters, renderers) to JSON.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe Project.uproject -run=NiagaraSystemExport -assets="/Game/Path/NS_A,/Game/Path/NS_B"
 *
 * Output:
 *   <ProjectDir>/Intermediate/UAssetExport/<AssetPath>.json
 */
UCLASS()
class UNiagaraSystemExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:

    UNiagaraSystemExportCommandlet();

    virtual int32 Main(const FString& Params) override;

private:

    TSharedPtr<FJsonObject> ExportNiagaraSystem(UNiagaraSystem* System) const;
    TSharedPtr<FJsonObject> ExportEmitter(const struct FNiagaraEmitterHandle& EmitterHandle) const;
    TSharedPtr<FJsonObject> ExportScript(class UNiagaraScript* Script, const FString& Usage) const;
    TSharedPtr<FJsonObject> ExportRendererProperties(class UNiagaraRendererProperties* Renderer) const;
    TSharedPtr<FJsonObject> ExportSubclassProperties(UObject* Object, UClass* StopAtClass) const;

};
