#pragma once

#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUAssetJsonExporter, Log, All);

class FUAssetJsonExporterModule : public IModuleInterface
{
public:

    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};

namespace UAssetJsonExporter
{
    // Returns true if a recent heartbeat is present (in-editor queue subsystem active).
    bool IsLiveEditorPresent();

    // Logs and returns true if a live editor was detected. Commandlets call this and
    // early-return to avoid fighting the editor for the project lock.
    bool AbortIfLiveEditor();

    // RAII scope: while alive, AbortIfLiveEditor() always returns false. Subsystem
    // wraps internal commandlet dispatch with this so its own heartbeat doesn't
    // make the dispatched commandlet refuse to run.
    struct FInternalDispatchScope
    {
        FInternalDispatchScope();
        ~FInternalDispatchScope();
    };
}
