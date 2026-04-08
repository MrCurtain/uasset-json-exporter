#include "BlueprintEdGraphExportCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectIterator.h"

#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

UBlueprintEdGraphExportCommandlet::UBlueprintEdGraphExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UBlueprintEdGraphExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - BlueprintEdGraphExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/BP_A,/Game/Path/BP_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!Blueprint)
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load Blueprint: %s"), *AssetPath);
            continue;
        }

        TSharedPtr<FJsonObject> JsonObject = ExportBlueprint(Blueprint);
        if (!JsonObject.IsValid())
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to export Blueprint: %s"), *AssetPath);
            continue;
        }

        FString OutputPath = GetExportPath(AssetPath);
        if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
        {
            UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
            ExportedCount++;
        }
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d blueprints exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UBlueprintEdGraphExportCommandlet::ExportBlueprint(UBlueprint* Blueprint) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("BlueprintEdGraph"));
    Root->SetStringField(TEXT("Blueprint"), Blueprint->GetName());
    Root->SetStringField(TEXT("AssetPath"), Blueprint->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    // Parent class
    if (Blueprint->ParentClass)
    {
        Root->SetStringField(TEXT("ParentClass"), Blueprint->ParentClass->GetName());
    }

    // Variables (member properties on generated class)
    TArray<TSharedPtr<FJsonValue>> VariablesArray;
    if (UClass* GeneratedClass = Blueprint->GeneratedClass)
    {
        UObject* CDO = GeneratedClass->GetDefaultObject();
        for (TFieldIterator<FProperty> PropIt(GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
            VarObj->SetStringField(TEXT("Name"), Property->GetName());
            VarObj->SetStringField(TEXT("Type"), Property->GetCPPType());

            if (CDO)
            {
                FString DefaultValue;
                Property->ExportTextItem_Direct(DefaultValue, Property->ContainerPtrToValuePtr<void>(CDO), nullptr, CDO, PPF_None);
                if (!DefaultValue.IsEmpty())
                {
                    VarObj->SetStringField(TEXT("Default"), DefaultValue);
                }
            }

            VariablesArray.Add(MakeShared<FJsonValueObject>(VarObj));
        }
    }
    Root->SetArrayField(TEXT("Variables"), VariablesArray);

    // Components (from SimpleConstructionScript)
    TArray<TSharedPtr<FJsonValue>> ComponentsArray;
    if (Blueprint->SimpleConstructionScript)
    {
        for (auto* SCSNode : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (SCSNode && SCSNode->ComponentTemplate)
            {
                TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
                CompObj->SetStringField(TEXT("Name"), SCSNode->GetVariableName().ToString());
                CompObj->SetStringField(TEXT("Class"), SCSNode->ComponentTemplate->GetClass()->GetName());
                ComponentsArray.Add(MakeShared<FJsonValueObject>(CompObj));
            }
        }
    }
    Root->SetArrayField(TEXT("Components"), ComponentsArray);

    // Graphs (EventGraphs + FunctionGraphs)
    TArray<TSharedPtr<FJsonValue>> GraphsArray;

    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        TSharedPtr<FJsonObject> GraphObj = ExportGraph(Graph);
        if (GraphObj.IsValid())
        {
            GraphObj->SetStringField(TEXT("GraphType"), TEXT("EventGraph"));
            GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
    }

    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        TSharedPtr<FJsonObject> GraphObj = ExportGraph(Graph);
        if (GraphObj.IsValid())
        {
            GraphObj->SetStringField(TEXT("GraphType"), TEXT("Function"));
            GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
    }

    Root->SetArrayField(TEXT("Graphs"), GraphsArray);

    // Referenced assets via AssetRegistry
    TArray<TSharedPtr<FJsonValue>> RefsArray;
    {
        IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<FName> Dependencies;
        AssetRegistry.GetDependencies(Blueprint->GetOutermost()->GetFName(), Dependencies);

        for (const FName& DepName : Dependencies)
        {
            FString DepPath = DepName.ToString();

            if (DepPath.StartsWith(TEXT("/Script/")) || DepPath.StartsWith(TEXT("/Engine/")))
            {
                continue;
            }

            TSharedPtr<FJsonObject> RefObj = MakeShared<FJsonObject>();
            RefObj->SetStringField(TEXT("PackageName"), DepPath);

            TArray<FAssetData> AssetDataList;
            AssetRegistry.GetAssetsByPackageName(DepName, AssetDataList, true);
            if (AssetDataList.Num() > 0)
            {
                RefObj->SetStringField(TEXT("AssetClass"), AssetDataList[0].AssetClassPath.GetAssetName().ToString());
            }

            RefsArray.Add(MakeShared<FJsonValueObject>(RefObj));
        }
    }
    Root->SetArrayField(TEXT("ReferencedAssets"), RefsArray);

    return Root;
}

TSharedPtr<FJsonObject> UBlueprintEdGraphExportCommandlet::ExportGraph(const UEdGraph* Graph) const
{
    if (!Graph)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
    GraphObj->SetStringField(TEXT("Name"), Graph->GetName());

    TArray<TSharedPtr<FJsonValue>> NodesArray;
    for (const UEdGraphNode* Node : Graph->Nodes)
    {
        TSharedPtr<FJsonObject> NodeObj = ExportNode(Node);
        if (NodeObj.IsValid())
        {
            NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
        }
    }
    GraphObj->SetArrayField(TEXT("Nodes"), NodesArray);

    return GraphObj;
}

TSharedPtr<FJsonObject> UBlueprintEdGraphExportCommandlet::ExportNode(const UEdGraphNode* Node) const
{
    if (!Node)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();

    NodeObj->SetStringField(TEXT("NodeId"), Node->NodeGuid.ToString());
    NodeObj->SetStringField(TEXT("Class"), Node->GetClass()->GetName());
    NodeObj->SetStringField(TEXT("Title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

    if (!Node->NodeComment.IsEmpty())
    {
        NodeObj->SetStringField(TEXT("Comment"), Node->NodeComment);
    }

    if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
    {
        FName FunctionName = CallNode->FunctionReference.GetMemberName();
        if (!FunctionName.IsNone())
        {
            NodeObj->SetStringField(TEXT("FunctionName"), FunctionName.ToString());
        }

        UClass* MemberParent = CallNode->FunctionReference.GetMemberParentClass();
        if (MemberParent)
        {
            NodeObj->SetStringField(TEXT("FunctionOwner"), MemberParent->GetName());
        }
    }

    if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
    {
        if (CastNode->TargetType)
        {
            NodeObj->SetStringField(TEXT("CastTarget"), CastNode->TargetType->GetPathName());
        }
    }

    if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
    {
        FName EventName = EventNode->EventReference.GetMemberName();
        if (!EventName.IsNone())
        {
            NodeObj->SetStringField(TEXT("EventName"), EventName.ToString());
        }
    }

    TArray<TSharedPtr<FJsonValue>> PinsArray;
    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->bHidden)
        {
            continue;
        }

        TSharedPtr<FJsonObject> PinObj = ExportPin(Pin);
        if (PinObj.IsValid())
        {
            PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));
        }
    }
    NodeObj->SetArrayField(TEXT("Pins"), PinsArray);

    return NodeObj;
}

TSharedPtr<FJsonObject> UBlueprintEdGraphExportCommandlet::ExportPin(const UEdGraphPin* Pin) const
{
    if (!Pin)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();

    PinObj->SetStringField(TEXT("Name"), Pin->PinName.ToString());
    PinObj->SetStringField(TEXT("Direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
    PinObj->SetStringField(TEXT("Type"), Pin->PinType.PinCategory.ToString());

    if (Pin->PinType.PinSubCategoryObject.IsValid())
    {
        PinObj->SetStringField(TEXT("SubType"), Pin->PinType.PinSubCategoryObject->GetName());
    }

    if (!Pin->DefaultValue.IsEmpty())
    {
        PinObj->SetStringField(TEXT("Default"), Pin->DefaultValue);
    }

    if (!Pin->DefaultTextValue.IsEmpty())
    {
        PinObj->SetStringField(TEXT("DefaultText"), Pin->DefaultTextValue.ToString());
    }

    if (Pin->DefaultObject)
    {
        PinObj->SetStringField(TEXT("DefaultObject"), Pin->DefaultObject->GetPathName());
    }

    if (Pin->LinkedTo.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> LinksArray;
        for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
        {
            if (LinkedPin && LinkedPin->GetOwningNode())
            {
                TSharedPtr<FJsonObject> LinkObj = MakeShared<FJsonObject>();
                LinkObj->SetStringField(TEXT("NodeId"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                LinkObj->SetStringField(TEXT("NodeTitle"), LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                LinkObj->SetStringField(TEXT("PinName"), LinkedPin->PinName.ToString());
                LinksArray.Add(MakeShared<FJsonValueObject>(LinkObj));
            }
        }
        PinObj->SetArrayField(TEXT("LinkedTo"), LinksArray);
    }

    return PinObj;
}

TArray<FString> UBlueprintEdGraphExportCommandlet::ParseAssetPaths(const FString& Params) const
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

FString UBlueprintEdGraphExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UBlueprintEdGraphExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
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
