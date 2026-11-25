#include "BlueprintWorkbenchAssistLibrary.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_Select.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_Self.h"
#include "BlueprintAssistTabHandler.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistCache.h"
#include "BlueprintAssistFormatters/FormatterInterface.h"
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_VariableSet.h"
#include "K2Node_VariableGet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_CallFunction.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "Logging/TokenizedMessage.h"
#include "GraphEditor.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, BlueprintWorkbenchAssist)

namespace
{
using FObject = TSharedRef<FJsonObject>;
FString Json(const FObject& Object)
{
    FString Text;
    FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
    return Text;
}
FString FailJson(const FString& Message)
{
    FObject Object = MakeShared<FJsonObject>();
    Object->SetBoolField(TEXT("success"), false);
    Object->SetStringField(TEXT("error"), Message);
    return Json(Object);
}
UEdGraph* FindGraph(UBlueprint* Blueprint, const FString& Path)
{
    if (!Blueprint) return nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    for (UEdGraph* Graph : Graphs)
        if (Graph->GetPathName(Blueprint) == Path) return Graph;
    return nullptr;
}
// BA 4.5 exposes all graph handlers; BA 4.2 exposes only the active one.
// OpenGraph focuses the requested document, and Handler still checks its exact graph.
template<typename T> auto GraphHandlers(T& Tabs, int) -> decltype(Tabs.GetAllGraphHandlers())
{
    return Tabs.GetAllGraphHandlers();
}
template<typename T> TArray<TSharedPtr<FBAGraphHandler>> GraphHandlers(T& Tabs, long)
{
    return { Tabs.GetActiveGraphHandler() };
}
TSharedPtr<FBAGraphHandler> Handler(const FString& Asset, const FString& Path)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Asset);
    UEdGraph* Graph = FindGraph(Blueprint, Path);
    if (!Graph || IsRunningCommandlet()) return nullptr;
    for (TSharedPtr<FBAGraphHandler> Candidate : GraphHandlers(FBATabHandler::Get(), 0))
        if (Candidate.IsValid() && Candidate->GetFocusedEdGraph() == Graph) return Candidate;
    return nullptr;
}
int32 MissingSizes(const TSharedPtr<FBAGraphHandler>& GraphHandler)
{
    int32 Missing = 0;
    for (UEdGraphNode* Node : GraphHandler->GetFocusedEdGraph()->Nodes)
        if (Node && !Cast<UEdGraphNode_Comment>(Node) && !Cast<UK2Node_Knot>(Node) && !GraphHandler->GetNodeData(Node).HasSize()) ++Missing;
    return Missing;
}
}

FString UBlueprintWorkbenchAssistLibrary::OpenGraph(const FString& AssetPath, const FString& GraphPath, bool AllowDirty)
{
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized()) return FailJson(TEXT("Requires a full Slate editor"));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    UEdGraph* Graph = FindGraph(Blueprint, GraphPath);
    if (!Graph) return FailJson(TEXT("Exact Blueprint graph path not found"));
    if (!AllowDirty && Blueprint->GetOutermost()->IsDirty()) return FailJson(TEXT("Asset is dirty; save or discard explicitly before starting"));
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    const bool PreviousForceFull = Blueprint->bForceFullEditor;
    Blueprint->bForceFullEditor = true; // Transient: data-only editors have no graph document tab.
    const bool Opened = Editors->OpenEditorForAsset(Blueprint);
    Blueprint->bForceFullEditor = PreviousForceFull;
    if (!Opened) return FailJson(TEXT("Unable to open Blueprint editor"));
    IAssetEditorInstance* Instance = Editors->FindEditorForAsset(Blueprint, false);
    // These editor toolkits derive from FBlueprintEditor (Blueprint, Widget and Animation editors).
    if (!Instance || (Instance->GetEditorName() != TEXT("BlueprintEditor") && Instance->GetEditorName() != TEXT("WidgetBlueprintEditor") && Instance->GetEditorName() != TEXT("AnimationBlueprintEditor"))) return FailJson(TEXT("Unsupported asset editor"));
    static_cast<FBlueprintEditor*>(Instance)->JumpToHyperlink(Graph, false);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetStringField(TEXT("graph"), Graph->GetPathName(Blueprint));
    return Json(Result);
}

FString UBlueprintWorkbenchAssistLibrary::GraphState(const FString& AssetPath, const FString& GraphPath, bool RefreshSizes)
{
    auto GraphHandler = Handler(AssetPath, GraphPath);
    if (!GraphHandler.IsValid()) return FailJson(TEXT("Waiting for Blueprint Assist graph handler"));
    if (RefreshSizes)
    {
        if (auto Window = GraphHandler->GetWindow()) Window->Resize(FVector2D(2560, 1440));
        GraphHandler->RefreshAllNodeSizes();
    }
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetNumberField(TEXT("missing_sizes"), MissingSizes(GraphHandler));
    Result->SetNumberField(TEXT("pending_sizes"), GraphHandler->GetNumberOfPendingNodesToCache());
    Result->SetBoolField(TEXT("busy"), GraphHandler->HasActiveTransaction());
    Result->SetBoolField(TEXT("ready"), MissingSizes(GraphHandler) == 0 && !GraphHandler->IsCalculatingNodeSize() && !GraphHandler->HasActiveTransaction());
    return Json(Result);
}

FString UBlueprintWorkbenchAssistLibrary::FormatFunctionGraph(const FString& AssetPath, const FString& GraphPath)
{
    auto GraphHandler = Handler(AssetPath, GraphPath);
    if (!GraphHandler.IsValid()) return FailJson(TEXT("Blueprint Assist graph handler missing"));
    UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
    UBlueprint* Blueprint = GraphHandler->GetBlueprint();
    if (!Blueprint || !Blueprint->FunctionGraphs.Contains(Graph)) return FailJson(TEXT("Bridge v0.1 supports function graphs only"));
    if (MissingSizes(GraphHandler) || GraphHandler->IsCalculatingNodeSize() || GraphHandler->HasActiveTransaction()) return FailJson(TEXT("Node size cache or another formatting transaction is pending"));
    UK2Node_FunctionEntry* Entry = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (auto* Found = Cast<UK2Node_FunctionEntry>(Node))
        {
            if (Entry) return FailJson(TEXT("Ambiguous function entry"));
            Entry = Found;
        }
    if (!Entry) return FailJson(TEXT("Function entry missing"));
    FScopedTransaction Transaction(NSLOCTEXT("BlueprintWorkbenchAssist", "Format", "Format function with Blueprint Assist"));
    Blueprint->Modify();
    GraphHandler->GetGraphEditor()->ClearSelectionSet();

    TSharedPtr<FFormatterInterface> Formatted = GraphHandler->FormatNodes(Entry);
    if (!Formatted.IsValid()) return FailJson(TEXT("Blueprint Assist formatter rejected the graph"));
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("saved"), false);
    Result->SetNumberField(TEXT("formatted_nodes"), Formatted->GetFormattedNodes().Num());
    return Json(Result);
}

FString UBlueprintWorkbenchAssistLibrary::FormatGraphRoot(const FString& AssetPath, const FString& GraphPath, const FString& NodeId)
{
    auto GraphHandler = Handler(AssetPath, GraphPath);
    if (!GraphHandler.IsValid()) return FailJson(TEXT("Blueprint Assist graph handler missing"));
    UEdGraph* Graph = GraphHandler->GetFocusedEdGraph();
    UBlueprint* Blueprint = GraphHandler->GetBlueprint();
    if (!Blueprint || !Cast<UEdGraphSchema_K2>(Graph->GetSchema())) return FailJson(TEXT("Only K2 graphs are supported"));
    if (MissingSizes(GraphHandler) || GraphHandler->IsCalculatingNodeSize() || GraphHandler->HasActiveTransaction()) return FailJson(TEXT("Node cache or transaction pending"));
    UEdGraphNode* Root = nullptr;
    for (UEdGraphNode* Node : Graph->Nodes)
        if (Node && Node->NodeGuid.ToString() == NodeId) Root = Node;
    if (!Root || !Cast<UK2Node>(Root)) return FailJson(TEXT("Exact K2 root node missing"));
    FScopedTransaction Transaction(NSLOCTEXT("BlueprintWorkbenchAssist", "FormatRoot", "Format selected root with Blueprint Assist"));
    Blueprint->Modify();
    GraphHandler->GetGraphEditor()->ClearSelectionSet();
    TSharedPtr<FFormatterInterface> Formatted = GraphHandler->FormatNodes(Root);
    if (!Formatted.IsValid()) return FailJson(TEXT("Blueprint Assist rejected root"));
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("saved"), false);
    Result->SetNumberField(TEXT("formatted_nodes"), Formatted->GetFormattedNodes().Num());
    return Json(Result);
}
FString UBlueprintWorkbenchAssistLibrary::RemoveUnusedNodes(const FString& AssetPath, const FString& GraphPath, const TArray<FString>& NodeIds)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    UEdGraph* Graph = FindGraph(Blueprint, GraphPath);
    if (!Graph || NodeIds.IsEmpty()) return FailJson(TEXT("Exact graph and nonempty removal set required"));
    TSet<UEdGraphNode*> Removal;
    for (const FString& Id : NodeIds)
    {
        UEdGraphNode* Node = nullptr;
        for (UEdGraphNode* Candidate : Graph->Nodes)
            if (Candidate && Candidate->NodeGuid.ToString() == Id) Node = Candidate;
        if (!Node || !(Cast<UK2Node_CallFunction>(Node) || Cast<UK2Node_IfThenElse>(Node) || Cast<UK2Node_VariableGet>(Node) || Cast<UK2Node_VariableSet>(Node) || Cast<UK2Node_DynamicCast>(Node) || Cast<UK2Node_Knot>(Node) || Cast<UK2Node_BreakStruct>(Node) || Cast<UK2Node_GetEnumeratorNameAsString>(Node) || Cast<UK2Node_Self>(Node) || Cast<UK2Node_MakeStruct>(Node) || Cast<UK2Node_Select>(Node) || Cast<UK2Node_EnumEquality>(Node) || Cast<UK2Node_SwitchInteger>(Node) || Cast<UK2Node_CallDelegate>(Node) || Cast<UK2Node_CreateDelegate>(Node) || Cast<UK2Node_SpawnActorFromClass>(Node) || Node->GetClass()->GetPathName() == TEXT("/Script/UMGEditor.K2Node_CreateWidget")))
            return FailJson(TEXT("Removal excludes entries, events, timelines, macros and unknown nodes"));
        if (auto* Call = Cast<UK2Node_CallFunction>(Node))
            if (!Call->GetTargetFunction() || Call->GetTargetFunction()->HasMetaData(TEXT("Latent"))) return FailJson(TEXT("Latent/unresolved call protected"));
        Removal.Add(Node);
    }
    for (UEdGraphNode* Node : Removal)
        for (UEdGraphPin* Pin : Node->Pins)
            for (UEdGraphPin* Link : Pin->LinkedTo)
                if (!Removal.Contains(Link->GetOwningNode()) && (Pin->Direction == EGPD_Output || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate))
                    return FailJson(TEXT("Live removal boundary; no nodes deleted"));
    FScopedTransaction Transaction(NSLOCTEXT("BlueprintWorkbenchAssist", "Prune", "Remove verified unused nodes"));
    Blueprint->Modify();
    for (UEdGraphNode* Node : Removal) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetBoolField(TEXT("saved"), false);
    Result->SetNumberField(TEXT("removed"), Removal.Num());
    return Json(Result);
}
FString UBlueprintWorkbenchAssistLibrary::RestoreFormattingDefaults(const FString& AssetPath, const FString& BlueprintJson)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    TSharedPtr<FJsonObject> Snapshot;
    if (!Blueprint || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(BlueprintJson), Snapshot) || !Snapshot.IsValid())
        return FailJson(TEXT("Blueprint and inspection snapshot required"));
    const TArray<TSharedPtr<FJsonValue>>* Graphs;
    if (!Snapshot->TryGetArrayField(TEXT("graphs"), Graphs)) return FailJson(TEXT("Snapshot graphs missing"));
    TArray<TSharedPtr<FJsonValue>> Changes;
    for (const auto& GraphValue : *Graphs)
    {
        auto GraphObject = GraphValue->AsObject();
        if (!GraphObject.IsValid()) return FailJson(TEXT("Invalid graph snapshot"));
        UEdGraph* Graph = FindGraph(Blueprint, GraphObject->GetStringField(TEXT("path")));
        if (!Graph) return FailJson(TEXT("Snapshot graph missing"));
        for (const auto& NodeValue : GraphObject->GetArrayField(TEXT("nodes")))
        {
            auto NodeObject = NodeValue->AsObject();
            UEdGraphNode* Node = nullptr;
            for (UEdGraphNode* Candidate : Graph->Nodes)
                if (Candidate && Candidate->NodeGuid.ToString() == NodeObject->GetStringField(TEXT("id"))) Node = Candidate;
            if (!Node) continue; // Transparent reroutes can be replaced by BA.
            for (const auto& PinValue : NodeObject->GetArrayField(TEXT("pins")))
            {
                auto PinObject = PinValue->AsObject();
                const EEdGraphPinDirection Direction = PinObject->GetStringField(TEXT("direction")) == TEXT("input") ? EGPD_Input : EGPD_Output;
                UEdGraphPin* Pin = Node->FindPin(FName(*PinObject->GetStringField(TEXT("name"))), Direction);
                if (!Pin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate) continue;
                const FString Original = PinObject->GetStringField(TEXT("default"));
                if (Pin->DefaultValue == Original) continue;
                FObject Change = MakeShared<FJsonObject>();
                Change->SetStringField(TEXT("graph"), GraphObject->GetStringField(TEXT("path")));
                Change->SetStringField(TEXT("node"), Node->NodeGuid.ToString());
                Change->SetStringField(TEXT("pin"), Pin->PinName.ToString());
                Change->SetStringField(TEXT("observed"), Pin->DefaultValue);
                Change->SetStringField(TEXT("restored"), Original);
                Changes.Add(MakeShared<FJsonValueObject>(Change));
                Node->Modify();
                Pin->DefaultValue = Original; // Restore the original value string; never preserve a formatting-side value edit. Caller verifies all remaining metadata and links.
            }
        }
    }
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), true);
    Result->SetArrayField(TEXT("changes"), Changes);
    return Json(Result);
}
FString UBlueprintWorkbenchAssistLibrary::CompileChecked(const FString& AssetPath)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint) return FailJson(TEXT("Blueprint not found"));
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    FObject Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), Log.NumErrors == 0 && Blueprint->Status != BS_Error);
    Result->SetBoolField(TEXT("compiled"), true);
    Result->SetNumberField(TEXT("compile_errors"), Log.NumErrors);
    Result->SetNumberField(TEXT("compile_warnings"), Log.NumWarnings);
    return Json(Result);
}

bool UBlueprintWorkbenchAssistLibrary::CaptureGraph(const FString& AssetPath, const FString& GraphPath, const FString& Filename, bool ZoomToFit)
{
    auto GraphHandler = Handler(AssetPath, GraphPath);
    if (!GraphHandler.IsValid() || !GraphHandler->GetGraphEditor().IsValid()) return false;
    if (ZoomToFit) { GraphHandler->GetGraphEditor()->ZoomToFit(false); return true; }
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!FSlateApplication::Get().TakeScreenshot(GraphHandler->GetGraphEditor().ToSharedRef(), Pixels, Size)) return false;
    TArray64<uint8> Png;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    return FFileHelper::SaveArrayToFile(Png, *Filename);
}
