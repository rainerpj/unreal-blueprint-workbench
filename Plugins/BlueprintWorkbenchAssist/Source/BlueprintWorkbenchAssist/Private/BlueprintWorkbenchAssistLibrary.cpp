#include "BlueprintWorkbenchAssistLibrary.h"
#include "BlueprintAssistTabHandler.h"
#include "BlueprintAssistGraphHandler.h"
#include "BlueprintAssistCache.h"
#include "BlueprintAssistFormatters/FormatterInterface.h"
#include "BlueprintEditor.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Knot.h"
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
TSharedPtr<FBAGraphHandler> Handler(const FString& Asset, const FString& Path)
{
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Asset);
    UEdGraph* Graph = FindGraph(Blueprint, Path);
    if (!Graph || IsRunningCommandlet()) return nullptr;
    for (TSharedPtr<FBAGraphHandler> Candidate : FBATabHandler::Get().GetAllGraphHandlers())
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

FString UBlueprintWorkbenchAssistLibrary::OpenGraph(const FString& AssetPath, const FString& GraphPath)
{
    if (IsRunningCommandlet() || !GEditor || !FSlateApplication::IsInitialized()) return FailJson(TEXT("Requires a full Slate editor"));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    UEdGraph* Graph = FindGraph(Blueprint, GraphPath);
    if (!Graph) return FailJson(TEXT("Exact Blueprint graph path not found"));
    if (Blueprint->GetOutermost()->IsDirty()) return FailJson(TEXT("Asset is dirty; save or discard explicitly before starting"));
    UAssetEditorSubsystem* Editors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
    if (!Editors->OpenEditorForAsset(Blueprint)) return FailJson(TEXT("Unable to open Blueprint editor"));
    IAssetEditorInstance* Instance = Editors->FindEditorForAsset(Blueprint, false);
    // Both editor toolkits derive from FBlueprintEditor (UE BlueprintEditor/UMGEditor).
    if (!Instance || (Instance->GetEditorName() != TEXT("BlueprintEditor") && Instance->GetEditorName() != TEXT("WidgetBlueprintEditor"))) return FailJson(TEXT("Unsupported asset editor"));
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
