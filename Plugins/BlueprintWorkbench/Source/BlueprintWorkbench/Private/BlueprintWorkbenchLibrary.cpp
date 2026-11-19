#include "BlueprintWorkbenchLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_GetEnumeratorNameAsString.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Animation/WidgetAnimation.h"

namespace BlueprintWorkbench
{
using FObject = TSharedPtr<FJsonObject>;
using FValues = TArray<TSharedPtr<FJsonValue>>;

static FString Json(const FObject& Object)
{
    FString Output;
    FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
    return Output;
}

static FString String(const FObject& Object, const TCHAR* Key)
{
    FString Value;
    Object->TryGetStringField(Key, Value);
    return Value;
}

static FObject Result()
{
    FObject Out = MakeShared<FJsonObject>();
    Out->SetBoolField(TEXT("success"), false);
    Out->SetBoolField(TEXT("compiled"), false);
    Out->SetBoolField(TEXT("saved"), false);
    return Out;
}

static FString Fail(const FObject& Out, const FString& Error)
{
    Out->SetStringField(TEXT("error"), Error);
    return Json(Out);
}

static FObject PinType(const FEdGraphPinType& Type)
{
    FObject Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("category"), Type.PinCategory.ToString());
    Out->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
    Out->SetStringField(TEXT("object"), GetPathNameSafe(Type.PinSubCategoryObject.Get()));
    Out->SetNumberField(TEXT("container"), static_cast<int32>(Type.ContainerType));
    Out->SetBoolField(TEXT("reference"), Type.bIsReference);
    Out->SetBoolField(TEXT("const"), Type.bIsConst);
    // Full UE export retains map value and other details beyond the compact fields above.
    FString Export;
    FEdGraphPinType::StaticStruct()->ExportText(Export, &Type, nullptr, nullptr, PPF_None, nullptr);
    Out->SetStringField(TEXT("unreal_text"), Export);
    return Out;
}

static FObject Describe(UBlueprint* Blueprint)
{
    FObject Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("asset"), Blueprint->GetPathName());
    Out->SetStringField(TEXT("parent"), GetPathNameSafe(Blueprint->ParentClass));
    Out->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetPathName());
    if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Blueprint))
    {
        FValues Widgets;
        if (WidgetBP->WidgetTree)
        {
            WidgetBP->WidgetTree->ForEachWidget([&Widgets](UWidget* Widget)
            {
                FObject Item = MakeShared<FJsonObject>();
                Item->SetStringField(TEXT("name"), Widget->GetName());
                Item->SetStringField(TEXT("class"), Widget->GetClass()->GetPathName());
                Item->SetStringField(TEXT("parent"), GetNameSafe(Widget->GetParent()));
                Widgets.Add(MakeShared<FJsonValueObject>(Item));
            });
        }
        Out->SetArrayField(TEXT("widgets"), Widgets);
        FValues Bindings;
        for (const FDelegateEditorBinding& Binding : WidgetBP->Bindings)
        {
            FString Export;
            FDelegateEditorBinding::StaticStruct()->ExportText(Export, &Binding, nullptr, WidgetBP, PPF_None, nullptr);
            Bindings.Add(MakeShared<FJsonValueString>(Export));
        }
        Out->SetArrayField(TEXT("widget_bindings"), Bindings);
        FValues Animations;
        for (UWidgetAnimation* Animation : WidgetBP->Animations)
            Animations.Add(MakeShared<FJsonValueString>(GetNameSafe(Animation)));
        Out->SetArrayField(TEXT("widget_animations"), Animations);
    }
    FValues Variables;
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        FObject Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Variable.VarName.ToString());
        Item->SetObjectField(TEXT("type"), PinType(Variable.VarType));
        Item->SetStringField(TEXT("default"), Variable.DefaultValue);
        Variables.Add(MakeShared<FJsonValueObject>(Item));
    }
    Out->SetArrayField(TEXT("variables"), Variables);
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    FValues GraphValues;
    for (UEdGraph* Graph : Graphs)
    {
        FObject Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Graph->GetName());
        Item->SetStringField(TEXT("path"), Graph->GetPathName(Blueprint));
        FValues Nodes;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node) continue;
            FObject NodeItem = MakeShared<FJsonObject>();
            NodeItem->SetStringField(TEXT("id"), Node->NodeGuid.ToString());
            NodeItem->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
            NodeItem->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
            NodeItem->SetNumberField(TEXT("x"), Node->NodePosX);
            NodeItem->SetNumberField(TEXT("y"), Node->NodePosY);
            NodeItem->SetNumberField(TEXT("width"), Node->NodeWidth);
            NodeItem->SetNumberField(TEXT("height"), Node->NodeHeight);
            NodeItem->SetStringField(TEXT("comment"), Node->NodeComment);
            if (UK2Node* K2Node = Cast<UK2Node>(Node)) NodeItem->SetBoolField(TEXT("pure"), K2Node->IsNodePure());
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
                NodeItem->SetBoolField(TEXT("latent"), !Call->GetTargetFunction() || Call->GetTargetFunction()->HasMetaData(TEXT("Latent")));
            if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
                NodeItem->SetStringField(TEXT("details"), Comment->NodeDetails.ToString());
            FValues Pins;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                FObject PinItem = MakeShared<FJsonObject>();
                PinItem->SetStringField(TEXT("id"), Pin->PinId.ToString());
                PinItem->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinItem->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                PinItem->SetObjectField(TEXT("type"), PinType(Pin->PinType));
                PinItem->SetStringField(TEXT("default"), Pin->DefaultValue);
                PinItem->SetStringField(TEXT("default_object"), GetPathNameSafe(Pin->DefaultObject));
                PinItem->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
                PinItem->SetBoolField(TEXT("orphaned"), Pin->bOrphanedPin);
                FValues Links;
                for (UEdGraphPin* Link : Pin->LinkedTo)
                {
                    FObject LinkItem = MakeShared<FJsonObject>();
                    LinkItem->SetStringField(TEXT("node"), Link->GetOwningNode()->NodeGuid.ToString());
                    LinkItem->SetStringField(TEXT("pin"), Link->PinName.ToString());
                    LinkItem->SetStringField(TEXT("pin_id"), Link->PinId.ToString());
                    Links.Add(MakeShared<FJsonValueObject>(LinkItem));
                }
                PinItem->SetArrayField(TEXT("links"), Links);
                Pins.Add(MakeShared<FJsonValueObject>(PinItem));
            }
            NodeItem->SetArrayField(TEXT("pins"), Pins);
            Nodes.Add(MakeShared<FJsonValueObject>(NodeItem));
        }
        Item->SetArrayField(TEXT("nodes"), Nodes);
        GraphValues.Add(MakeShared<FJsonValueObject>(Item));
    }
    Out->SetArrayField(TEXT("graphs"), GraphValues);
    return Out;
}

struct FPatch
{
    UBlueprint* Blueprint;
    TMap<FString, UEdGraphNode*> Aliases;
    FString Error;
    TMap<FString, UEdGraphNode*> SourceNodes;

    bool Reject(const FString& Message) { Error = Message; return false; }

    UEdGraph* Graph(const FString& Name)
    {
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        UEdGraph* Found = nullptr;
        for (UEdGraph* Candidate : Graphs)
        {
            if (Candidate->GetName() == Name || Candidate->GetPathName(Blueprint) == Name)
            {
                if (Found) { Reject(TEXT("Ambiguous graph: ") + Name); return nullptr; }
                Found = Candidate;
            }
        }
        if (!Found || !Cast<UEdGraphSchema_K2>(Found->GetSchema()))
        {
            Reject(TEXT("Missing or unsupported K2 graph: ") + Name);
            return nullptr;
        }
        return Found;
    }

    UEdGraphNode* ResolveNode(UEdGraph* InGraph, const FString& NodeId)
    {
        UEdGraphNode* Found = Aliases.FindRef(NodeId);
        if (!Found)
        {
            FGuid Guid;
            if (FGuid::Parse(NodeId, Guid))
            {
                Found = SourceNodes.FindRef(InGraph->GetPathName(Blueprint) + TEXT("|") + Guid.ToString());
                if (!Found)
                    for (UEdGraphNode* Node : InGraph->Nodes)
                        if (Node && Node->NodeGuid == Guid) { Found = Node; break; }
            }
        }
        if (!Found || Found->GetGraph() != InGraph)
        {
            Reject(TEXT("Node not found in selected graph: ") + NodeId);
            return nullptr;
        }
        return Found;
    }

    UEdGraphPin* Pin(UEdGraph* InGraph, const FObject& Endpoint, EEdGraphPinDirection Direction)
    {
        const FString NodeId = String(Endpoint, TEXT("node"));
        UEdGraphNode* Found = ResolveNode(InGraph, NodeId);
        if (!Found) return nullptr;
        const FString Name = String(Endpoint, TEXT("pin"));
        const FString PinId = String(Endpoint, TEXT("pin_id"));
        FGuid Guid;
        const bool bById = !PinId.IsEmpty();
        if (bById && !FGuid::Parse(PinId, Guid)) { Reject(TEXT("Invalid pin_id")); return nullptr; }
        UEdGraphPin* Selected = nullptr;
        for (UEdGraphPin* Candidate : Found->Pins)
        {
            if (Candidate->Direction == Direction && !Candidate->bOrphanedPin &&
                (bById ? Candidate->PinId == Guid : Candidate->PinName.ToString() == Name))
            {
                if (Selected) { Reject(TEXT("Ambiguous pin; use pin_id")); return nullptr; }
                Selected = Candidate;
            }
        }
        if (!Selected) Reject(TEXT("Pin not found: ") + NodeId + TEXT(".") + Name);
        return Selected;
    }

    bool NewName(const FString& Name)
    {
        return (!Name.IsEmpty() && FKismetNameValidator(Blueprint).IsValid(Name) == EValidatorResult::Ok)
            || Reject(TEXT("Invalid or existing member name: ") + Name);
    }

    bool Variable(const FObject& Op)
    {
        const FString Name = String(Op, TEXT("name"));
        if (!NewName(Name)) return false;
        const FString Type = String(Op, TEXT("type"));
        FEdGraphPinType PinType;
        if (Type == TEXT("bool")) PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        else if (Type == TEXT("int")) PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
        else if (Type == TEXT("float") || Type == TEXT("double"))
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
            PinType.PinSubCategory = Type == TEXT("float") ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Double;
        }
        else if (Type == TEXT("string")) PinType.PinCategory = UEdGraphSchema_K2::PC_String;
        else if (Type == TEXT("name")) PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
        else if (Type == TEXT("struct"))
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            PinType.PinSubCategoryObject = LoadObject<UScriptStruct>(nullptr, *String(Op, TEXT("object")));
            if (!PinType.PinSubCategoryObject.IsValid()) return Reject(TEXT("Struct type not found"));
        }
        else if (Type == TEXT("object"))
        {
            PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
            PinType.PinSubCategoryObject = LoadObject<UClass>(nullptr, *String(Op, TEXT("object")));
            if (!PinType.PinSubCategoryObject.IsValid()) return Reject(TEXT("Object class not found"));
        }
        else return Reject(TEXT("Unsupported variable type: ") + Type);
        // Defaults are edited through typed input pins; member variables use their normal zero defaults.
        if (Op->HasField(TEXT("default"))) return Reject(TEXT("Member defaults are not supported; use set_default on a setter input"));
        return FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), PinType)
            || Reject(TEXT("AddMemberVariable failed"));
    }

    bool Override(const FObject& Op)
    {
        const FString Name = String(Op, TEXT("name"));
        UFunction* Function = Blueprint->ParentClass->FindFunctionByName(FName(*Name));
        if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintEvent) || Function->HasAnyFunctionFlags(FUNC_Final))
            return Reject(TEXT("Not an overridable Blueprint event: ") + Name);
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Existing : Graphs)
        {
            if (Existing->GetFName() == Function->GetFName()) return Reject(TEXT("Override graph already exists"));
            for (UEdGraphNode* Node : Existing->Nodes)
                if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
                    if (Event->EventReference.GetMemberName() == Function->GetFName()) return Reject(TEXT("Override event already exists"));
        }
        // Functions with outputs (e.g. PrepareLegacyRound) get a function graph and return node.
        if (UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function))
        {
            UEdGraph* TargetGraph = Graph(String(Op, TEXT("graph")));
            if (!TargetGraph) return false;
            if (!Blueprint->UbergraphPages.Contains(TargetGraph)) return Reject(TEXT("Override events require an event graph"));
            FGraphNodeCreator<UK2Node_Event> Creator(*TargetGraph);
            UK2Node_Event* Node = Creator.CreateNode();
            Node->EventReference.SetExternalMember(Function->GetFName(), Function->GetOwnerClass());
            Node->bOverrideFunction = true;
            Creator.Finalize();
        }
        else
        {
            UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            // The UFunction overload creates a new signature; the class overload retains override ownership.
            FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, NewGraph, false, Function->GetOwnerClass());
        }
        return true;
    }

    template<class T, class F> UEdGraphNode* Create(UEdGraph* TargetGraph, F Configure)
    {
        FGraphNodeCreator<T> Creator(*TargetGraph);
        T* Node = Creator.CreateNode();
        Configure(Node);
        Creator.Finalize();
        return Node;
    }

    bool Node(const FObject& Op, UEdGraph* TargetGraph)
    {
        const FString Id = String(Op, TEXT("id"));
        FGuid Guid;
        if (Id.IsEmpty() || Aliases.Contains(Id) || FGuid::Parse(Id, Guid)) return Reject(TEXT("New node id must be a unique non-Guid alias"));
        const FString Kind = String(Op, TEXT("kind"));
        UEdGraphNode* Added = nullptr;
        if (Kind == TEXT("branch")) Added = Create<UK2Node_IfThenElse>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("comment"))
        {
            double Width = 0, Height = 0;
            if (!Op->TryGetNumberField(TEXT("width"), Width) || !Op->TryGetNumberField(TEXT("height"), Height) ||
                !FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width < 100 || Height < 100 || Width > 100000 || Height > 100000)
                return Reject(TEXT("Comment requires finite width/height between 100 and 100000"));
            if (String(Op, TEXT("text")).IsEmpty()) return Reject(TEXT("Comment text is required"));
            UEdGraphNode_Comment* Comment = CastChecked<UEdGraphNode_Comment>(Create<UEdGraphNode_Comment>(TargetGraph, [](auto*) {}));
            Comment->NodeComment = String(Op, TEXT("text"));
            Comment->NodeDetails = FText::FromString(String(Op, TEXT("details")));
            Comment->NodeWidth = Width;
            Comment->NodeHeight = Height;
            Comment->FontSize = 20;
            Comment->MoveMode = ECommentBoxMode::GroupMovement;
            Comment->CommentColor = FLinearColor(0.12f, 0.24f, 0.32f, 1.0f);
            const FValues* Members = nullptr;
            if (Op->TryGetArrayField(TEXT("members"), Members))
                for (const auto& Member : *Members)
                {
                    FString MemberId;
                    if (!Member->TryGetString(MemberId)) return Reject(TEXT("Comment member must be a node id"));
                    UEdGraphNode* MemberNode = ResolveNode(TargetGraph, MemberId);
                    if (!MemberNode) return false;
                    Comment->AddNodeUnderComment(MemberNode);
                }
            Added = Comment;
        }
        else if (Kind == TEXT("break_struct"))
        {
            UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *String(Op, TEXT("struct")));
            // CanBeBroken is not exported by BlueprintGraph in installed UE builds.
            if (!Struct || Struct->HasMetaData(TEXT("HasNativeBreak")) || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
                return Reject(TEXT("Struct cannot be broken by a generic Break Struct node"));
            Added = Create<UK2Node_BreakStruct>(TargetGraph, [Struct](auto* NewNode) { NewNode->StructType = Struct; });
            if (Added->Pins.Num() < 2) return Reject(TEXT("Struct has no exposed output properties"));
        }
        else if (Kind == TEXT("enum_to_string"))
            Added = Create<UK2Node_GetEnumeratorNameAsString>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("call"))
        {
            const FString OwnerPath = String(Op, TEXT("owner"));
            UClass* Owner = OwnerPath.IsEmpty() ? Blueprint->SkeletonGeneratedClass.Get() : LoadObject<UClass>(nullptr, *OwnerPath);
            UFunction* Function = Owner ? Owner->FindFunctionByName(FName(*String(Op, TEXT("function")))) : nullptr;
            if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
                return Reject(TEXT("Blueprint callable function not found"));
            if (Function->HasMetaData(TEXT("BlueprintInternalUseOnly")) || Function->HasMetaData(TEXT("CustomStructureParam")) || Function->HasMetaData(TEXT("ArrayParm")))
                return Reject(TEXT("This function needs a specialized node; generic calls are not supported"));
            if (Function->HasMetaData(TEXT("Latent")) && !Blueprint->UbergraphPages.Contains(TargetGraph))
                return Reject(TEXT("Latent call requires an event graph"));
            Added = Create<UK2Node_CallFunction>(TargetGraph, [Function](auto* NewNode) { NewNode->SetFromFunction(Function); });
        }
        else if (Kind == TEXT("get") || Kind == TEXT("set"))
        {
            const FName Name(*String(Op, TEXT("variable")));
            const FString OwnerPath = String(Op, TEXT("owner"));
            UClass* Owner = OwnerPath.IsEmpty() ? Blueprint->SkeletonGeneratedClass.Get() : LoadObject<UClass>(nullptr, *OwnerPath);
            FProperty* Property = Owner ? FindFProperty<FProperty>(Owner, Name) : nullptr;
            if (!Property || !Property->HasAnyPropertyFlags(CPF_BlueprintVisible)) return Reject(TEXT("Visible member variable not found"));
            if (Kind == TEXT("set") && Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) return Reject(TEXT("Variable is read-only"));
            auto Configure = [Name, Owner, OwnerPath](auto* NewNode)
            {
                if (OwnerPath.IsEmpty()) NewNode->VariableReference.SetSelfMember(Name);
                else NewNode->VariableReference.SetExternalMember(Name, Owner);
            };
            Added = Kind == TEXT("get") ? Create<UK2Node_VariableGet>(TargetGraph, Configure) : Create<UK2Node_VariableSet>(TargetGraph, Configure);
        }
        else if (Kind == TEXT("custom_event"))
        {
            const FString Name = String(Op, TEXT("name"));
            if (!Blueprint->UbergraphPages.Contains(TargetGraph)) return Reject(TEXT("Custom events require an event graph"));
            if (!NewName(Name)) return false;
            Added = Create<UK2Node_CustomEvent>(TargetGraph, [&Name](auto* NewNode) { NewNode->CustomFunctionName = FName(*Name); });
        }
        else return Reject(TEXT("Unsupported node kind: ") + Kind);
        double X = 0, Y = 0;
        Op->TryGetNumberField(TEXT("x"), X);
        Op->TryGetNumberField(TEXT("y"), Y);
        Added->NodePosX = FMath::Clamp(X, -1000000.0, 1000000.0);
        Added->NodePosY = FMath::Clamp(Y, -1000000.0, 1000000.0);
        Aliases.Add(Id, Added);
        return true;
    }

    bool Apply(const FObject& Op)
    {
        const FString Kind = String(Op, TEXT("op"));
        // Catch misspelled/unsupported fields rather than silently applying a different patch.
        TArray<FString> Fields;
        if (Kind == TEXT("add_variable")) Fields = {TEXT("op"), TEXT("name"), TEXT("type"), TEXT("object")};
        else if (Kind == TEXT("add_override")) Fields = {TEXT("op"), TEXT("name"), TEXT("graph")};
        else if (Kind == TEXT("add_node")) Fields = {TEXT("op"), TEXT("graph"), TEXT("kind"), TEXT("id"), TEXT("x"), TEXT("y"), TEXT("owner"), TEXT("function"), TEXT("variable"), TEXT("name"), TEXT("struct"), TEXT("text"), TEXT("details"), TEXT("width"), TEXT("height"), TEXT("members")};
        else if (Kind == TEXT("move_node")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("x"), TEXT("y")};
        else if (Kind == TEXT("remove_unused_nodes")) Fields = {TEXT("op"), TEXT("graph"), TEXT("nodes")};
        else if (Kind == TEXT("set_default")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("pin"), TEXT("pin_id"), TEXT("value")};
        else if (Kind == TEXT("connect") || Kind == TEXT("disconnect")) Fields = {TEXT("op"), TEXT("graph"), TEXT("from"), TEXT("to")};
        else return Reject(TEXT("Unsupported operation: ") + Kind);
        for (const auto& Field : Op->Values)
            if (!Fields.Contains(Field.Key)) return Reject(TEXT("Unsupported field: ") + Field.Key);
        if (Kind == TEXT("add_variable")) return Variable(Op);
        if (Kind == TEXT("add_override")) return Override(Op);
        UEdGraph* TargetGraph = Graph(String(Op, TEXT("graph")));
        if (!TargetGraph) return false;
        if (Kind == TEXT("move_node"))
        {
            UEdGraphNode* Existing = ResolveNode(TargetGraph, String(Op, TEXT("node")));
            double X = 0, Y = 0;
            if (!Existing) return false;
            if (!Op->TryGetNumberField(TEXT("x"), X) || !Op->TryGetNumberField(TEXT("y"), Y) ||
                !FMath::IsFinite(X) || !FMath::IsFinite(Y) || FMath::Abs(X) > 1000000 || FMath::Abs(Y) > 1000000)
                return Reject(TEXT("Move requires finite x/y within graph bounds"));
            Existing->NodePosX = FMath::RoundToInt(X);
            Existing->NodePosY = FMath::RoundToInt(Y);
            return true;
        }
        if (Kind == TEXT("remove_unused_nodes"))
        {
            const FValues* Ids = nullptr;
            if (!Op->TryGetArrayField(TEXT("nodes"), Ids) || Ids->IsEmpty()) return Reject(TEXT("Explicit nonempty removal set required"));
            TSet<UEdGraphNode*> Removal;
            for (const auto& Id : *Ids)
            {
                FString NodeId;
                if (!Id->TryGetString(NodeId)) return Reject(TEXT("Removal node must be an id"));
                UEdGraphNode* Existing = ResolveNode(TargetGraph, NodeId);
                if (!Existing) return false;
                const bool bOrdinary = Cast<UK2Node_CallFunction>(Existing) || Cast<UK2Node_IfThenElse>(Existing) ||
                    Cast<UK2Node_VariableGet>(Existing) || Cast<UK2Node_VariableSet>(Existing) || Cast<UK2Node_DynamicCast>(Existing) ||
                    Cast<UK2Node_Knot>(Existing) || Cast<UK2Node_BreakStruct>(Existing) || Cast<UK2Node_GetEnumeratorNameAsString>(Existing);
                if (!bOrdinary) return Reject(TEXT("Removal excludes entries, events, timelines, macros and unknown nodes"));
                if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Existing))
                {
                    UFunction* Function = Call->GetTargetFunction();
                    if (!Function || Function->HasMetaData(TEXT("Latent"))) return Reject(TEXT("Removal excludes latent or unresolved calls"));
                }
                Removal.Add(Existing);
            }
            for (UEdGraphNode* Existing : Removal)
                for (UEdGraphPin* Pin : Existing->Pins)
                    for (UEdGraphPin* Link : Pin->LinkedTo)
                        if (!Removal.Contains(Link->GetOwningNode()) && (Pin->Direction == EGPD_Output ||
                            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate))
                            return Reject(TEXT("Removal set has a live boundary connection; nothing removed"));
            for (UEdGraphNode* Existing : Removal)
            {
                for (auto It = SourceNodes.CreateIterator(); It; ++It) if (It.Value() == Existing) It.RemoveCurrent();
                for (auto It = Aliases.CreateIterator(); It; ++It) if (It.Value() == Existing) It.RemoveCurrent();
                FBlueprintEditorUtils::RemoveNode(Blueprint, Existing, true);
            }
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            return true;
        }
        if (Kind == TEXT("add_node"))
        {
            if (!Node(Op, TargetGraph)) return false;
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            return true;
        }
        const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(TargetGraph->GetSchema());
        if (Kind == TEXT("set_default"))
        {
            UEdGraphPin* Input = Pin(TargetGraph, Op, EGPD_Input);
            if (!Input) return false;
            FString Value;
            if (!Op->TryGetStringField(TEXT("value"), Value)) return Reject(TEXT("value must be a string"));
            if (!Input->LinkedTo.IsEmpty() || Input->bDefaultValueIsIgnored || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                return Reject(TEXT("Default requires an unconnected data input"));
            // Object and localized text defaults require different UE setters, intentionally excluded in v0.1.
            const FName Category = Input->PinType.PinCategory;
            // K2 accepts arbitrary bool text as false; reject typos at this external-input boundary.
            if (Category == UEdGraphSchema_K2::PC_Boolean && Value != TEXT("true") && Value != TEXT("false"))
                return Reject(TEXT("Boolean defaults must be true or false"));
            if (Category != UEdGraphSchema_K2::PC_Boolean && Category != UEdGraphSchema_K2::PC_Int &&
                Category != UEdGraphSchema_K2::PC_Real && Category != UEdGraphSchema_K2::PC_String &&
                Category != UEdGraphSchema_K2::PC_Name && Category != UEdGraphSchema_K2::PC_Struct && Category != UEdGraphSchema_K2::PC_Byte)
                return Reject(TEXT("Unsupported pin default type"));
            const FString Invalid = Schema->IsPinDefaultValid(Input, Value, nullptr, FText());
            if (!Invalid.IsEmpty()) return Reject(Invalid);
            Schema->TrySetDefaultValue(*Input, Value);
            return true;
        }
        if (Kind == TEXT("connect") || Kind == TEXT("disconnect"))
        {
            const FObject* From = nullptr;
            const FObject* To = nullptr;
            if (!Op->TryGetObjectField(TEXT("from"), From) || !Op->TryGetObjectField(TEXT("to"), To))
                return Reject(TEXT("Connection requires from and to objects"));
            UEdGraphPin* Output = Pin(TargetGraph, *From, EGPD_Output);
            if (!Output) return false;
            UEdGraphPin* Input = Pin(TargetGraph, *To, EGPD_Input);
            if (!Input) return false;
            if (Kind == TEXT("disconnect"))
            {
                if (!Output->LinkedTo.Contains(Input)) return Reject(TEXT("Requested link does not exist"));
                Schema->BreakSinglePinLink(Output, Input);
                return true;
            }
            if (Output->LinkedTo.Contains(Input)) return true;
            const FPinConnectionResponse Response = Schema->CanCreateConnection(Output, Input);
            // Never implicitly break another link or insert a conversion node.
            if (Response.Response != CONNECT_RESPONSE_MAKE) return Reject(TEXT("Connection refused: ") + Response.Message.ToString());
            return Schema->TryCreateConnection(Output, Input) || Reject(TEXT("TryCreateConnection failed"));
        }
        return Reject(TEXT("Unsupported operation: ") + Kind);
    }
};
}

FString UBlueprintWorkbenchLibrary::InspectBlueprint(const FString& AssetPath)
{
    using namespace BlueprintWorkbench;
    FObject Out = Result();
    if (!IsInGameThread()) return Fail(Out, TEXT("Must run on the editor game thread"));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Blueprint) return Fail(Out, TEXT("Blueprint asset not found"));
    Out->SetBoolField(TEXT("success"), true);
    Out->SetObjectField(TEXT("blueprint"), Describe(Blueprint));
    return Json(Out);
}

static FString ApplyPatchInternal(const FString& AssetPath, const FString& PatchJson, const FString& DestinationPackage, bool bWriteSource)
{
    using namespace BlueprintWorkbench;
    FObject Out = Result();
    if (!IsInGameThread()) return Fail(Out, TEXT("Must run on the editor game thread"));
    FObject Patch;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(PatchJson), Patch) || !Patch.IsValid())
        return Fail(Out, TEXT("Patch must be a JSON object"));
    const FValues* Operations = nullptr;
    if (!Patch->TryGetArrayField(TEXT("operations"), Operations)) return Fail(Out, TEXT("Missing operations array"));
    if (Patch->Values.Num() != 1) return Fail(Out, TEXT("Patch only accepts the operations field"));
    UBlueprint* Source = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!Source || !Source->ParentClass) return Fail(Out, TEXT("Blueprint or parent not found"));
    // UMG graph edits use the registered Widget compiler; Designer mutation remains out of scope.
    if ((Source->GetClass() != UBlueprint::StaticClass() && Source->GetClass() != UWidgetBlueprint::StaticClass()) || Source->BlueprintType != BPTYPE_Normal)
        return Fail(Out, TEXT("Only normal Blueprint and Widget Blueprint assets are supported"));
    const bool bSaveCopy = !DestinationPackage.IsEmpty();
    const bool bSave = bWriteSource || bSaveCopy;
    if (bSaveCopy && (!DestinationPackage.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(DestinationPackage)))
        return Fail(Out, TEXT("Destination must be a new /Game package name without object suffix"));
    if (bSaveCopy && (FindPackage(nullptr, *DestinationPackage) || FPackageName::DoesPackageExist(DestinationPackage)))
        return Fail(Out, TEXT("Destination already exists; overwriting is not supported"));
    FString BackupFilename;
    if (bWriteSource)
    {
        if (!IsRunningCommandlet() || !Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))
            return Fail(Out, TEXT("Source writeback requires a commandlet and an existing /Game asset"));
        const FString SourceFilename = FPackageName::LongPackageNameToFilename(Source->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        if (!IFileManager::Get().FileExists(*SourceFilename)) return Fail(Out, TEXT("Source asset is not saved on disk"));
        for (const TCHAR* Extension : {TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl")})
            if (IFileManager::Get().FileExists(*FPaths::ChangeExtension(SourceFilename, Extension)))
                return Fail(Out, TEXT("Writeback of split/bulk packages is not supported"));
        BackupFilename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BlueprintWorkbench/Backups") /
            FGuid::NewGuid().ToString(EGuidFormats::Digits) / FPaths::GetCleanFilename(SourceFilename));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(BackupFilename), true);
        if (IFileManager::Get().Copy(*BackupFilename, *SourceFilename, false) != COPY_OK)
            return Fail(Out, TEXT("Could not back up source; nothing edited"));
        Out->SetStringField(TEXT("backup_file"), BackupFilename);
    }
    const FString PackageName = bWriteSource ? Source->GetOutermost()->GetName() :
        (bSaveCopy ? DestinationPackage : TEXT("/Temp/BlueprintWorkbench_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UPackage* Package = bWriteSource ? Source->GetOutermost() : CreatePackage(*PackageName);
    TStrongObjectPtr<UPackage> KeepPackage(Package);
    if (!bSave) Package->SetFlags(RF_Transient);
    TMap<UObject*, UObject*> DuplicatedObjects;
    FObjectDuplicationParameters Duplication(Source, Package);
    Duplication.DestName = FName(*FPackageName::GetLongPackageAssetName(PackageName));
    Duplication.CreatedObjects = &DuplicatedObjects;
    UBlueprint* Copy = bWriteSource ? Source : Cast<UBlueprint>(StaticDuplicateObjectEx(Duplication));
    if (!Copy) return Fail(Out, TEXT("Blueprint duplication failed"));
    TStrongObjectPtr<UBlueprint> KeepCopy(Copy);
    Copy->SetFlags(RF_Public | RF_Standalone);
    FPatch Context{Copy, {}, {}, {}};
    // UE regenerates NodeGuid during duplication. Resolve source endpoints through its exact
    // object map rather than overwriting the engine-assigned identity of the copied node.
    for (const auto& Pair : DuplicatedObjects)
    {
        UEdGraphNode* SourceNode = Cast<UEdGraphNode>(Pair.Key);
        UEdGraphNode* CopyNode = Cast<UEdGraphNode>(Pair.Value);
        if (SourceNode && CopyNode && SourceNode->IsIn(Source) && CopyNode->IsIn(Copy))
            Context.SourceNodes.Add(SourceNode->GetGraph()->GetPathName(Source) + TEXT("|") + SourceNode->NodeGuid.ToString(), CopyNode);
    }
    bool bPatched = true;
    for (int32 Index = 0; Index < Operations->Num(); ++Index)
    {
        const TSharedPtr<FJsonValue>& Value = (*Operations)[Index];
        if (Value->Type != EJson::Object || !Context.Apply(Value->AsObject()))
        {
            Out->SetNumberField(TEXT("failed_operation"), Index);
            Out->SetStringField(TEXT("error"), Value->Type != EJson::Object ? TEXT("Operation must be an object") : Context.Error);
            bPatched = false;
            break;
        }
    }
    if (bPatched)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Copy);
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Copy, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        Out->SetBoolField(TEXT("compiled"), true);
        Out->SetNumberField(TEXT("compile_errors"), Log.NumErrors);
        Out->SetNumberField(TEXT("compile_warnings"), Log.NumWarnings);
        FValues Messages;
        for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
            Messages.Add(MakeShared<FJsonValueString>(Message->ToText().ToString()));
        Out->SetArrayField(TEXT("compile_messages"), Messages);
        if (Log.NumErrors == 0 && Copy->Status != BS_Error)
        {
            Out->SetObjectField(TEXT("blueprint"), Describe(Copy));
            FObject Aliases = MakeShared<FJsonObject>();
            for (const auto& Pair : Context.Aliases) Aliases->SetStringField(Pair.Key, Pair.Value->NodeGuid.ToString());
            Out->SetObjectField(TEXT("aliases"), Aliases);
            FObject SourceNodes = MakeShared<FJsonObject>();
            for (const auto& Pair : Context.SourceNodes) SourceNodes->SetStringField(Pair.Key, Pair.Value->NodeGuid.ToString());
            Out->SetObjectField(TEXT("source_nodes"), SourceNodes);
            if (bSave)
            {
                FSavePackageArgs Args;
                Args.TopLevelFlags = RF_Public | RF_Standalone;
                Args.SaveFlags = SAVE_NoError;
                const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
                const bool bSaved = UPackage::SavePackage(Package, Copy, *Filename, Args);
                Out->SetBoolField(TEXT("saved"), bSaved);
                Out->SetBoolField(TEXT("success"), bSaved);
                if (bSaved && !bWriteSource) FAssetRegistryModule::AssetCreated(Copy);
                if (!bSaved)
                {
                    if (bWriteSource)
                    {
                        const bool bRestored = IFileManager::Get().Copy(*Filename, *BackupFilename, true, true) == COPY_OK;
                        Out->SetBoolField(TEXT("backup_restored"), bRestored);
                    }
                    Out->SetStringField(TEXT("error"), TEXT("SavePackage failed; inspect backup restoration and the editor log"));
                }
            }
            else Out->SetBoolField(TEXT("success"), true);
        }
        else Out->SetStringField(TEXT("error"), TEXT("Patched copy did not compile; nothing saved"));
    }
    if (!bWriteSource && (!bSave || !Out->GetBoolField(TEXT("saved"))))
    {
        // Failed/preview copies must not appear in Save All. No source package is changed or saved.
        Copy->ClearFlags(RF_Public | RF_Standalone);
        Package->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
        if (bSave)
        {
            const FString DiscardName = TEXT("/Temp/BlueprintWorkbench_Discard_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Package->Rename(*DiscardName, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
        }
    }
    return Json(Out);
}

FString UBlueprintWorkbenchLibrary::ApplyPatch(const FString& AssetPath, const FString& PatchJson, const FString& DestinationPackage)
{
    return ApplyPatchInternal(AssetPath, PatchJson, DestinationPackage, false);
}

FString UBlueprintWorkbenchLibrary::ApplyPatchToSavedAsset(const FString& AssetPath, const FString& PatchJson)
{
    using namespace BlueprintWorkbench;
    if (!IsRunningCommandlet()) return Fail(Result(), TEXT("Source writeback is commandlet-only; close other editors first"));
    const FString Preview = ApplyPatchInternal(AssetPath, PatchJson, TEXT(""), false);
    FObject PreviewResult;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Preview), PreviewResult) || !PreviewResult->GetBoolField(TEXT("success")))
        return Preview;
    return ApplyPatchInternal(AssetPath, PatchJson, TEXT(""), true);
}
