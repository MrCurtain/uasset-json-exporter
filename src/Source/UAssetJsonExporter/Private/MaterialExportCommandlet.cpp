#include "MaterialExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UMaterialExportCommandlet::UMaterialExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UMaterialExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - MaterialExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/M_A,/Game/Path/MI_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        // Try MaterialInstance first, then Material
        UMaterialInstance* MaterialInstance = LoadObject<UMaterialInstance>(nullptr, *AssetPath);
        if (MaterialInstance)
        {
            TSharedPtr<FJsonObject> JsonObject = ExportMaterialInstance(MaterialInstance);
            if (JsonObject.IsValid())
            {
                FString OutputPath = GetExportPath(AssetPath);
                if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
                {
                    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
                    ExportedCount++;
                }
            }
            continue;
        }

        UMaterial* Material = LoadObject<UMaterial>(nullptr, *AssetPath);
        if (Material)
        {
            TSharedPtr<FJsonObject> JsonObject = ExportMaterial(Material);
            if (JsonObject.IsValid())
            {
                FString OutputPath = GetExportPath(AssetPath);
                if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
                {
                    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
                    ExportedCount++;
                }
            }
            continue;
        }

        UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load Material or MaterialInstance: %s"), *AssetPath);
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d materials exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UMaterialExportCommandlet::ExportMaterial(UMaterial* Material) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("Material"));
    Root->SetStringField(TEXT("MaterialName"), Material->GetName());
    Root->SetStringField(TEXT("AssetPath"), Material->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    // Global settings
    Root->SetStringField(TEXT("ShadingModel"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue(static_cast<int64>(Material->GetShadingModels().GetFirstShadingModel())));
    Root->SetStringField(TEXT("BlendMode"), StaticEnum<EBlendMode>()->GetNameStringByValue(static_cast<int64>(Material->BlendMode)));
    Root->SetBoolField(TEXT("TwoSided"), Material->IsTwoSided());
    Root->SetStringField(TEXT("MaterialDomain"), StaticEnum<EMaterialDomain>()->GetNameStringByValue(static_cast<int64>(Material->MaterialDomain)));

    // Expressions (node graph)
    TArray<TSharedPtr<FJsonValue>> ExpressionsArray;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression)
        {
            TSharedPtr<FJsonObject> ExprObj = ExportExpression(Expression);
            if (ExprObj.IsValid())
            {
                ExpressionsArray.Add(MakeShared<FJsonValueObject>(ExprObj));
            }
        }
    }
    Root->SetArrayField(TEXT("Expressions"), ExpressionsArray);

    // Expression connections (input pins on the material output node)
    TSharedPtr<FJsonObject> OutputConnections = MakeShared<FJsonObject>();

    auto ExportMaterialInput = [&](const FExpressionInput& Input, const FString& PinName)
    {
        if (Input.Expression)
        {
            TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
            ConnObj->SetStringField(TEXT("Expression"), Input.Expression->GetName());
            ConnObj->SetStringField(TEXT("ExpressionClass"), Input.Expression->GetClass()->GetName());
            ConnObj->SetNumberField(TEXT("OutputIndex"), Input.OutputIndex);
            OutputConnections->SetObjectField(PinName, ConnObj);
        }
    };

    ExportMaterialInput(Material->GetEditorOnlyData()->BaseColor, TEXT("BaseColor"));
    ExportMaterialInput(Material->GetEditorOnlyData()->Metallic, TEXT("Metallic"));
    ExportMaterialInput(Material->GetEditorOnlyData()->Specular, TEXT("Specular"));
    ExportMaterialInput(Material->GetEditorOnlyData()->Roughness, TEXT("Roughness"));
    ExportMaterialInput(Material->GetEditorOnlyData()->Normal, TEXT("Normal"));
    ExportMaterialInput(Material->GetEditorOnlyData()->EmissiveColor, TEXT("EmissiveColor"));
    ExportMaterialInput(Material->GetEditorOnlyData()->Opacity, TEXT("Opacity"));
    ExportMaterialInput(Material->GetEditorOnlyData()->OpacityMask, TEXT("OpacityMask"));
    ExportMaterialInput(Material->GetEditorOnlyData()->WorldPositionOffset, TEXT("WorldPositionOffset"));
    ExportMaterialInput(Material->GetEditorOnlyData()->AmbientOcclusion, TEXT("AmbientOcclusion"));

    if (OutputConnections->Values.Num() > 0)
    {
        Root->SetObjectField(TEXT("OutputConnections"), OutputConnections);
    }

    return Root;
}

TSharedPtr<FJsonObject> UMaterialExportCommandlet::ExportMaterialInstance(UMaterialInstance* MaterialInstance) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("MaterialInstance"));
    Root->SetStringField(TEXT("MaterialInstanceName"), MaterialInstance->GetName());
    Root->SetStringField(TEXT("AssetPath"), MaterialInstance->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    // Parent material
    if (MaterialInstance->Parent)
    {
        Root->SetStringField(TEXT("Parent"), MaterialInstance->Parent->GetName());
        Root->SetStringField(TEXT("ParentPath"), MaterialInstance->Parent->GetPathName());
    }

    // Scalar parameters
    TArray<TSharedPtr<FJsonValue>> ScalarsArray;
    for (const FScalarParameterValue& Param : MaterialInstance->ScalarParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("Name"), Param.ParameterInfo.Name.ToString());
        ParamObj->SetNumberField(TEXT("Value"), Param.ParameterValue);
        ScalarsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }
    Root->SetArrayField(TEXT("ScalarParameters"), ScalarsArray);

    // Vector parameters
    TArray<TSharedPtr<FJsonValue>> VectorsArray;
    for (const FVectorParameterValue& Param : MaterialInstance->VectorParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("Name"), Param.ParameterInfo.Name.ToString());
        ParamObj->SetStringField(TEXT("Value"), Param.ParameterValue.ToString());
        VectorsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }
    Root->SetArrayField(TEXT("VectorParameters"), VectorsArray);

    // Texture parameters
    TArray<TSharedPtr<FJsonValue>> TexturesArray;
    for (const FTextureParameterValue& Param : MaterialInstance->TextureParameterValues)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("Name"), Param.ParameterInfo.Name.ToString());
        if (Param.ParameterValue)
        {
            ParamObj->SetStringField(TEXT("Texture"), Param.ParameterValue->GetName());
            ParamObj->SetStringField(TEXT("TexturePath"), Param.ParameterValue->GetPathName());
        }
        else
        {
            ParamObj->SetStringField(TEXT("Texture"), TEXT("None"));
        }
        TexturesArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }
    Root->SetArrayField(TEXT("TextureParameters"), TexturesArray);

    // Static Switch parameters
    TArray<TSharedPtr<FJsonValue>> StaticSwitchArray;
    for (const FStaticSwitchParameter& Param : MaterialInstance->GetStaticParameters().StaticSwitchParameters)
    {
        TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
        ParamObj->SetStringField(TEXT("Name"), Param.ParameterInfo.Name.ToString());
        ParamObj->SetBoolField(TEXT("Value"), Param.Value);
        ParamObj->SetBoolField(TEXT("Override"), Param.bOverride);
        StaticSwitchArray.Add(MakeShared<FJsonValueObject>(ParamObj));
    }
    Root->SetArrayField(TEXT("StaticSwitchParameters"), StaticSwitchArray);

    return Root;
}

TSharedPtr<FJsonObject> UMaterialExportCommandlet::ExportExpression(UMaterialExpression* Expression) const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

    Obj->SetStringField(TEXT("Name"), Expression->GetName());
    Obj->SetStringField(TEXT("Class"), Expression->GetClass()->GetName());
    Obj->SetStringField(TEXT("Description"), Expression->GetDescription());

    // Parameter-specific data
    if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
    {
        Obj->SetStringField(TEXT("ParameterName"), ScalarParam->ParameterName.ToString());
        Obj->SetNumberField(TEXT("DefaultValue"), ScalarParam->DefaultValue);
        Obj->SetStringField(TEXT("Group"), ScalarParam->Group.ToString());
    }
    else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
    {
        Obj->SetStringField(TEXT("ParameterName"), VectorParam->ParameterName.ToString());
        Obj->SetStringField(TEXT("DefaultValue"), VectorParam->DefaultValue.ToString());
        Obj->SetStringField(TEXT("Group"), VectorParam->Group.ToString());
    }
    else if (UMaterialExpressionTextureSampleParameter* TextureParam = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
    {
        Obj->SetStringField(TEXT("ParameterName"), TextureParam->ParameterName.ToString());
        if (TextureParam->Texture)
        {
            Obj->SetStringField(TEXT("DefaultTexture"), TextureParam->Texture->GetPathName());
        }
        Obj->SetStringField(TEXT("Group"), TextureParam->Group.ToString());
    }

    // Input connections
    {
        TArray<TSharedPtr<FJsonValue>> InputsArray;
        for (FExpressionInputIterator It(Expression); It; ++It)
        {
            FExpressionInput* Input = It.Input;
            if (Input && Input->Expression)
            {
                TSharedPtr<FJsonObject> InputObj = MakeShared<FJsonObject>();
                InputObj->SetStringField(TEXT("InputName"), Expression->GetInputName(It.Index).ToString());
                InputObj->SetStringField(TEXT("ConnectedTo"), Input->Expression->GetName());
                InputObj->SetStringField(TEXT("ConnectedClass"), Input->Expression->GetClass()->GetName());
                InputObj->SetNumberField(TEXT("OutputIndex"), Input->OutputIndex);
                InputsArray.Add(MakeShared<FJsonValueObject>(InputObj));
            }
        }
        if (InputsArray.Num() > 0)
        {
            Obj->SetArrayField(TEXT("Inputs"), InputsArray);
        }
    }

    return Obj;
}

TArray<FString> UMaterialExportCommandlet::ParseAssetPaths(const FString& Params) const
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

FString UMaterialExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UMaterialExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
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
