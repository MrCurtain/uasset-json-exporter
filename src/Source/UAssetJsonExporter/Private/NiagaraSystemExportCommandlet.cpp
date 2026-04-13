#include "NiagaraSystemExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "NiagaraRendererProperties.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UNiagaraSystemExportCommandlet::UNiagaraSystemExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UNiagaraSystemExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - NiagaraSystemExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/NS_A,/Game/Path/NS_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *AssetPath);
        if (!System)
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load NiagaraSystem: %s"), *AssetPath);
            continue;
        }

        TSharedPtr<FJsonObject> JsonObject = ExportNiagaraSystem(System);
        if (!JsonObject.IsValid())
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to export NiagaraSystem: %s"), *AssetPath);
            continue;
        }

        FString OutputPath = GetExportPath(AssetPath);
        if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
        {
            UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
            ExportedCount++;
        }
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d Niagara systems exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UNiagaraSystemExportCommandlet::ExportNiagaraSystem(UNiagaraSystem* System) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("NiagaraSystem"));
    Root->SetStringField(TEXT("SystemName"), System->GetName());
    Root->SetStringField(TEXT("AssetPath"), System->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    // System-level exposed user parameters
    TArray<TSharedPtr<FJsonValue>> UserParamsArray;
    const FNiagaraUserRedirectionParameterStore& UserParams = System->GetExposedParameters();

    for (const FNiagaraVariableWithOffset& VarWithOffset : UserParams.ReadParameterVariables())
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("Name"), VarWithOffset.GetName().ToString());
        ParamObj->SetStringField(TEXT("Type"), VarWithOffset.GetType().GetName());
        UserParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }
    Root->SetArrayField(TEXT("ExposedParameters"), UserParamsArray);

    // Emitters
    TArray<TSharedPtr<FJsonValue>> EmittersArray;
    for (const FNiagaraEmitterHandle& EmitterHandle : System->GetEmitterHandles())
    {
        TSharedPtr<FJsonObject> EmitterObj = ExportEmitter(EmitterHandle);
        if (EmitterObj.IsValid())
        {
            EmittersArray.Add(MakeShared<FJsonValueObject>(EmitterObj));
        }
    }
    Root->SetArrayField(TEXT("Emitters"), EmittersArray);

    return Root;
}

TSharedPtr<FJsonObject> UNiagaraSystemExportCommandlet::ExportEmitter(const FNiagaraEmitterHandle& EmitterHandle) const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

    Obj->SetStringField(TEXT("Name"), EmitterHandle.GetName().ToString());
    Obj->SetBoolField(TEXT("Enabled"), EmitterHandle.GetIsEnabled());
    Obj->SetStringField(TEXT("UniqueInstanceName"), EmitterHandle.GetUniqueInstanceName());

    FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetEmitterData();
    if (!EmitterData)
    {
        return Obj;
    }

    // Sim target
    Obj->SetStringField(TEXT("SimTarget"), EmitterData->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));

    // Scripts per usage stage
    if (EmitterData->SpawnScriptProps.Script)
    {
        Obj->SetObjectField(TEXT("SpawnScript"), ExportScript(EmitterData->SpawnScriptProps.Script, TEXT("Spawn")));
    }
    if (EmitterData->UpdateScriptProps.Script)
    {
        Obj->SetObjectField(TEXT("UpdateScript"), ExportScript(EmitterData->UpdateScriptProps.Script, TEXT("Update")));
    }

    // Renderers
    TArray<TSharedPtr<FJsonValue>> RenderersArray;
    for (UNiagaraRendererProperties* Renderer : EmitterData->GetRenderers())
    {
        if (Renderer)
        {
            TSharedPtr<FJsonObject> RendererObj = ExportRendererProperties(Renderer);
            if (RendererObj.IsValid())
            {
                RenderersArray.Add(MakeShared<FJsonValueObject>(RendererObj));
            }
        }
    }
    Obj->SetArrayField(TEXT("Renderers"), RenderersArray);

    return Obj;
}

TSharedPtr<FJsonObject> UNiagaraSystemExportCommandlet::ExportScript(UNiagaraScript* Script, const FString& Usage) const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

    Obj->SetStringField(TEXT("Usage"), Usage);
    Obj->SetStringField(TEXT("ScriptName"), Script->GetName());

    // Script parameters (inputs)
    TArray<TSharedPtr<FJsonValue>> ParamsArray;
    const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
    if (VMData.IsValid())
    {
        for (const FNiagaraVariable& Param : VMData.Parameters.Parameters)
        {
            TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
            ParamObj->SetStringField(TEXT("Name"), Param.GetName().ToString());
            ParamObj->SetStringField(TEXT("Type"), Param.GetType().GetName());
            ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
        }
    }
    Obj->SetArrayField(TEXT("Parameters"), ParamsArray);

    return Obj;
}

TSharedPtr<FJsonObject> UNiagaraSystemExportCommandlet::ExportRendererProperties(UNiagaraRendererProperties* Renderer) const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

    Obj->SetStringField(TEXT("RendererClass"), Renderer->GetClass()->GetName());

    TSharedPtr<FJsonObject> Props = ExportSubclassProperties(Renderer, UNiagaraRendererProperties::StaticClass());
    if (Props.IsValid() && Props->Values.Num() > 0)
    {
        Obj->SetObjectField(TEXT("Properties"), Props);
    }

    return Obj;
}

TSharedPtr<FJsonObject> UNiagaraSystemExportCommandlet::ExportSubclassProperties(UObject* Object, UClass* StopAtClass) const
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

            FString Value;
            Prop->ExportTextItem_Direct(Value, Prop->ContainerPtrToValuePtr<void>(Object), nullptr, Object, PPF_None);
            if (!Value.IsEmpty())
            {
                Props->SetStringField(Prop->GetName(), Value);
            }
        }
        CurrentClass = CurrentClass->GetSuperClass();
    }

    return Props;
}

TArray<FString> UNiagaraSystemExportCommandlet::ParseAssetPaths(const FString& Params) const
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

FString UNiagaraSystemExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UNiagaraSystemExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
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
