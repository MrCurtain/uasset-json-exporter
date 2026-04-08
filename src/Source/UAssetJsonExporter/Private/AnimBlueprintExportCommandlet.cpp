#include "AnimBlueprintExportCommandlet.h"
#include "UAssetJsonExporterModule.h"
#include "UAssetJsonExporterVersion.h"

#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UAnimBlueprintExportCommandlet::UAnimBlueprintExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UAnimBlueprintExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogUAssetJsonExporter, Display, TEXT("UAssetJsonExporter v%s - AnimBlueprintExport"), UASSET_JSON_EXPORTER_VERSION_STRING);

    TArray<FString> AssetPaths = ParseAssetPaths(Params);

    if (AssetPaths.IsEmpty())
    {
        UE_LOG(LogUAssetJsonExporter, Error, TEXT("No assets specified. Usage: -assets=\"/Game/Path/ABP_A,/Game/Path/ABP_B\""));
        return 1;
    }

    int32 ExportedCount = 0;

    for (const FString& AssetPath : AssetPaths)
    {
        UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
        if (!AnimBP)
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to load AnimBlueprint: %s"), *AssetPath);
            continue;
        }

        TSharedPtr<FJsonObject> JsonObject = ExportAnimBlueprint(AnimBP);
        if (!JsonObject.IsValid())
        {
            UE_LOG(LogUAssetJsonExporter, Warning, TEXT("Failed to export AnimBlueprint: %s"), *AssetPath);
            continue;
        }

        FString OutputPath = GetExportPath(AssetPath);
        if (SaveJsonToFile(JsonObject.ToSharedRef(), OutputPath))
        {
            UE_LOG(LogUAssetJsonExporter, Display, TEXT("Exported: %s -> %s"), *AssetPath, *OutputPath);
            ExportedCount++;
        }
    }

    UE_LOG(LogUAssetJsonExporter, Display, TEXT("Export complete. %d/%d anim blueprints exported."), ExportedCount, AssetPaths.Num());
    return 0;
}

TSharedPtr<FJsonObject> UAnimBlueprintExportCommandlet::ExportAnimBlueprint(UAnimBlueprint* AnimBP) const
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

    Root->SetStringField(TEXT("ExporterVersion"), UASSET_JSON_EXPORTER_VERSION_STRING);
    Root->SetStringField(TEXT("ExportType"), TEXT("AnimBlueprint"));
    Root->SetStringField(TEXT("AnimBlueprintName"), AnimBP->GetName());
    Root->SetStringField(TEXT("AssetPath"), AnimBP->GetPathName());
    Root->SetStringField(TEXT("ExportTimestamp"), FDateTime::Now().ToString());

    if (AnimBP->ParentClass)
    {
        Root->SetStringField(TEXT("ParentClass"), AnimBP->ParentClass->GetName());
    }

    if (AnimBP->TargetSkeleton)
    {
        Root->SetStringField(TEXT("TargetSkeleton"), AnimBP->TargetSkeleton->GetPathName());
    }

    // EdGraphs (EventGraph + AnimGraph functions)
    TArray<TSharedPtr<FJsonValue>> GraphsArray;

    for (UEdGraph* Graph : AnimBP->UbergraphPages)
    {
        TSharedPtr<FJsonObject> GraphObj = ExportGraph(Graph);
        if (GraphObj.IsValid())
        {
            GraphObj->SetStringField(TEXT("GraphType"), TEXT("EventGraph"));
            GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
    }

    for (UEdGraph* Graph : AnimBP->FunctionGraphs)
    {
        TSharedPtr<FJsonObject> GraphObj = ExportGraph(Graph);
        if (GraphObj.IsValid())
        {
            GraphObj->SetStringField(TEXT("GraphType"), TEXT("AnimGraph"));
            GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
        }
    }

    Root->SetArrayField(TEXT("Graphs"), GraphsArray);

    // StateMachines — find all StateMachine nodes across all graphs and export their internal structure
    TArray<TSharedPtr<FJsonValue>> StateMachinesArray;

    auto FindStateMachines = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs)
    {
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph)
            {
                continue;
            }

            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UAnimGraphNode_StateMachineBase* SMNode = Cast<UAnimGraphNode_StateMachineBase>(Node);
                if (SMNode && SMNode->EditorStateMachineGraph)
                {
                    TSharedPtr<FJsonObject> SMObj = ExportStateMachine(SMNode->EditorStateMachineGraph);
                    if (SMObj.IsValid())
                    {
                        SMObj->SetStringField(TEXT("StateMachineName"), SMNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                        StateMachinesArray.Add(MakeShared<FJsonValueObject>(SMObj));
                    }
                }
            }
        }
    };

    FindStateMachines(AnimBP->FunctionGraphs);
    FindStateMachines(AnimBP->UbergraphPages);

    Root->SetArrayField(TEXT("StateMachines"), StateMachinesArray);

    return Root;
}

TSharedPtr<FJsonObject> UAnimBlueprintExportCommandlet::ExportStateMachine(UAnimationStateMachineGraph* SMGraph) const
{
    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

    // States
    TArray<TSharedPtr<FJsonValue>> StatesArray;

    // Transitions
    TArray<TSharedPtr<FJsonValue>> TransitionsArray;

    for (UEdGraphNode* Node : SMGraph->Nodes)
    {
        if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
        {
            TSharedPtr<FJsonObject> StateObj = MakeShared<FJsonObject>();
            StateObj->SetStringField(TEXT("StateName"), StateNode->GetStateName());
            StateObj->SetStringField(TEXT("NodeId"), StateNode->NodeGuid.ToString());

            if (!StateNode->NodeComment.IsEmpty())
            {
                StateObj->SetStringField(TEXT("Comment"), StateNode->NodeComment);
            }

            // Export the state's bound graph (contains the animation logic)
            if (StateNode->BoundGraph)
            {
                TSharedPtr<FJsonObject> BoundGraphObj = ExportGraph(StateNode->BoundGraph);
                if (BoundGraphObj.IsValid())
                {
                    StateObj->SetObjectField(TEXT("BoundGraph"), BoundGraphObj);
                }
            }

            StatesArray.Add(MakeShared<FJsonValueObject>(StateObj));
        }
        else if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
        {
            TSharedPtr<FJsonObject> TransObj = MakeShared<FJsonObject>();
            TransObj->SetStringField(TEXT("NodeId"), TransNode->NodeGuid.ToString());

            UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
            UAnimStateNodeBase* NextState = TransNode->GetNextState();

            if (PrevState)
            {
                TransObj->SetStringField(TEXT("FromState"), PrevState->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            }
            if (NextState)
            {
                TransObj->SetStringField(TEXT("ToState"), NextState->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            }

            TransObj->SetNumberField(TEXT("CrossfadeDuration"), TransNode->CrossfadeDuration);
            TransObj->SetStringField(TEXT("BlendMode"), StaticEnum<EAlphaBlendOption>()->GetNameStringByValue(static_cast<int64>(TransNode->BlendMode)));

            // Export transition rule graph
            if (TransNode->BoundGraph)
            {
                TSharedPtr<FJsonObject> RuleGraph = ExportGraph(TransNode->BoundGraph);
                if (RuleGraph.IsValid())
                {
                    TransObj->SetObjectField(TEXT("TransitionRule"), RuleGraph);
                }
            }

            TransitionsArray.Add(MakeShared<FJsonValueObject>(TransObj));
        }
    }

    Obj->SetArrayField(TEXT("States"), StatesArray);
    Obj->SetArrayField(TEXT("Transitions"), TransitionsArray);

    return Obj;
}

// EdGraph export

TSharedPtr<FJsonObject> UAnimBlueprintExportCommandlet::ExportGraph(const UEdGraph* Graph) const
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

TSharedPtr<FJsonObject> UAnimBlueprintExportCommandlet::ExportNode(const UEdGraphNode* Node) const
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

TSharedPtr<FJsonObject> UAnimBlueprintExportCommandlet::ExportPin(const UEdGraphPin* Pin) const
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

// Shared utilities

TArray<FString> UAnimBlueprintExportCommandlet::ParseAssetPaths(const FString& Params) const
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

FString UAnimBlueprintExportCommandlet::GetExportPath(const FString& AssetPath) const
{
    FString RelativePath = AssetPath;
    RelativePath.RemoveFromStart(TEXT("/"));

    return FPaths::Combine(FPaths::ProjectDir(), TEXT("Intermediate"), TEXT("UAssetExport"), RelativePath + TEXT(".json"));
}

bool UAnimBlueprintExportCommandlet::SaveJsonToFile(const TSharedRef<FJsonObject>& JsonObject, const FString& FilePath) const
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
