#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"
#include "AssetExportQueueProtocol.h"

#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/Timespan.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogUAssetJsonExporter);

void FUAssetJsonExporterModule::StartupModule()
{
    UE_LOG(LogUAssetJsonExporter, Log, TEXT("UAssetJsonExporter v%s loaded."), UASSET_JSON_EXPORTER_VERSION_STRING);
}

void FUAssetJsonExporterModule::ShutdownModule()
{
}

bool UAssetJsonExporter::IsLiveEditorPresent()
{
    const FString AlivePath = FPaths::Combine(
        FPaths::ProjectDir(),
        UAssetExportQueue::QueueRootRelative,
        UAssetExportQueue::AliveFileName);

    if (!IFileManager::Get().FileExists(*AlivePath))
    {
        return false;
    }

    const FDateTime ModTime = IFileManager::Get().GetTimeStamp(*AlivePath);
    if (ModTime == FDateTime::MinValue())
    {
        return false;
    }

    const FTimespan Age = FDateTime::UtcNow() - ModTime;
    return Age.GetTotalSeconds() <= UAssetExportQueue::HeartbeatFreshnessSeconds;
}

static int32 GInternalDispatchDepth = 0;

UAssetJsonExporter::FInternalDispatchScope::FInternalDispatchScope()
{
    ++GInternalDispatchDepth;
}

UAssetJsonExporter::FInternalDispatchScope::~FInternalDispatchScope()
{
    --GInternalDispatchDepth;
}

bool UAssetJsonExporter::AbortIfLiveEditor()
{
    if (GInternalDispatchDepth > 0)
    {
        return false;
    }
    if (!IsLiveEditorPresent())
    {
        return false;
    }
    UE_LOG(LogUAssetJsonExporter, Error,
        TEXT("Live editor session detected. Refusing commandlet run to avoid project lock conflict. ")
        TEXT("Close the editor, or let the in-editor queue subsystem handle the export."));
    return true;
}

IMPLEMENT_MODULE(FUAssetJsonExporterModule, UAssetJsonExporter)
