#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogUAssetJsonExporter);

#define LOCTEXT_NAMESPACE "FUAssetJsonExporterModule"

void FUAssetJsonExporterModule::StartupModule()
{
    UE_LOG(LogUAssetJsonExporter, Log, TEXT("UAssetJsonExporter v%s loaded."), UASSET_JSON_EXPORTER_VERSION_STRING);
}

void FUAssetJsonExporterModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUAssetJsonExporterModule, UAssetJsonExporter)
