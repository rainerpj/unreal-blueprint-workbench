#include "BlueprintWorkbenchLibrary.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_Select.h"

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
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Message.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SetFieldsInStruct.h"
#include "KismetCompilerMisc.h"
#include "UObject/StructOnScope.h"
#include "UObject/Interface.h"
#include "K2Node_Self.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/SceneComponent.h"
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
#include "UObject/MetaData.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "K2Node_ConstructObjectFromClass.h"
#include "K2Node_GetSubsystem.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "K2Node_FormatText.h"
#include "K2Node_CallParentFunction.h"
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

// Layout may duplicate only direct self/member reads, never arbitrary pure calls.
static bool CanLocalizeRead(UEdGraphNode* Node)
{
    if (!Node) return false;
    const UK2Node* K2 = Cast<UK2Node>(Node);
    if (!K2 || !K2->IsNodePure()) return false;
    if (!Cast<UK2Node_Self>(Node))
    {
        UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node);
        if (!Get || !Get->VariableReference.IsSelfContext()) return false;
        FProperty* Property = Get->GetPropertyForVariableFromSkeleton();
        if (!Property || Property->HasMetaData(TEXT("BlueprintGetter"))) return false;
    }
    int32 Outputs = 0;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->bOrphanedPin || Pin->ParentPin || Pin->SubPins.Num() ||
            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec ||
            Pin->PinType.bIsReference || Pin->PinType.IsContainer()) return false;
        if (Pin->Direction == EGPD_Input && (Pin->LinkedTo.Num() || Pin->DefaultObject || !Pin->DefaultValue.IsEmpty())) return false;
        if (Pin->Direction == EGPD_Output)
        {
            ++Outputs;
            for (UEdGraphPin* Target : Pin->LinkedTo)
                if (!Target || Target->PinType.bIsReference) return false;
        }
    }
    return Outputs == 1;
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

// UE 5.5 returns UMetaData*, UE 5.7 returns FMetaData&. Preserve native comment
// classes; older engines store the missing Details field as per-object package metadata.
template<typename T> static T& Metadata(T* Value) { return *Value; }
template<typename T> static T& Metadata(T& Value) { return Value; }

static FString CommentDetails(UEdGraphNode_Comment* Comment)
{
    if (FTextProperty* Property = FindFProperty<FTextProperty>(Comment->GetClass(), TEXT("NodeDetails")))
        return Property->GetPropertyValue_InContainer(Comment).ToString();
    return Metadata(Comment->GetOutermost()->GetMetaData()).GetValue(Comment, TEXT("BlueprintWorkbench.CommentDetails"));
}

static void SetCommentDetails(UEdGraphNode_Comment* Comment, const FString& Details)
{
    if (FTextProperty* Property = FindFProperty<FTextProperty>(Comment->GetClass(), TEXT("NodeDetails")))
        Property->SetPropertyValue_InContainer(Comment, FText::FromString(Details));
    else
        Metadata(Comment->GetOutermost()->GetMetaData()).SetValue(Comment, TEXT("BlueprintWorkbench.CommentDetails"), *Details);
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
    Out->SetNumberField(TEXT("blueprint_type"), Blueprint->BlueprintType);
    FValues Interfaces;
    for (const auto& I : Blueprint->ImplementedInterfaces) Interfaces.Add(MakeShared<FJsonValueString>(GetPathNameSafe(I.Interface)));
    Out->SetArrayField(TEXT("interfaces"), Interfaces);
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
            NodeItem->SetBoolField(TEXT("localizable_read"), CanLocalizeRead(Node));
            if (UK2Node* K2Node = Cast<UK2Node>(Node)) NodeItem->SetBoolField(TEXT("pure"), K2Node->IsNodePure());
            if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
                NodeItem->SetBoolField(TEXT("latent"), !Call->GetTargetFunction() || Call->GetTargetFunction()->HasMetaData(TEXT("Latent")));
            if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
                NodeItem->SetStringField(TEXT("details"), CommentDetails(Comment));
            if (auto* Entry = Cast<UK2Node_FunctionEntry>(Node))
            {
                NodeItem->SetBoolField(TEXT("call_in_editor"), Entry->MetaData.bCallInEditor);
                NodeItem->SetNumberField(TEXT("function_flags"), Entry->GetExtraFlags());
                NodeItem->SetStringField(TEXT("category"), Entry->MetaData.Category.ToString());
                NodeItem->SetStringField(TEXT("tooltip"), Entry->MetaData.ToolTip.ToString());
                FValues Locals;
                for (const FBPVariableDescription& Local : Entry->LocalVariables)
                {
                    FObject L = MakeShared<FJsonObject>();
                    L->SetStringField(TEXT("name"), Local.VarName.ToString());
                    L->SetObjectField(TEXT("type"), PinType(Local.VarType));
                    L->SetStringField(TEXT("default"), Local.DefaultValue);
                    Locals.Add(MakeShared<FJsonValueObject>(L));
                }
                NodeItem->SetArrayField(TEXT("local_variables"), Locals);
            }
            if (auto* Event = Cast<UK2Node_Event>(Node))
            {
                NodeItem->SetBoolField(TEXT("event_override"), Event->bOverrideFunction);
                NodeItem->SetStringField(TEXT("event_name"), Event->EventReference.GetMemberName().ToString());
                NodeItem->SetStringField(TEXT("event_owner"), GetPathNameSafe(Event->EventReference.GetMemberParentClass(Blueprint->SkeletonGeneratedClass)));
            }
            if (auto* Event = Cast<UK2Node_CustomEvent>(Node))
                NodeItem->SetBoolField(TEXT("call_in_editor"), Event->bCallInEditor);
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
        if (NodeId == TEXT("$entry") || NodeId == TEXT("$return"))
            for (UEdGraphNode* N : InGraph->Nodes)
                if ((NodeId == TEXT("$entry") && Cast<UK2Node_FunctionEntry>(N)) || (NodeId == TEXT("$return") && Cast<UK2Node_FunctionResult>(N))) { Found = N; break; }
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

    bool ReadType(const FObject& Spec, FEdGraphPinType& Type)
    {
        const FString Kind = String(Spec, TEXT("type"));
        if (Kind == TEXT("bool")) Type.PinCategory = UEdGraphSchema_K2::PC_Boolean;
        else if (Kind == TEXT("int")) Type.PinCategory = UEdGraphSchema_K2::PC_Int;
        else if (Kind == TEXT("int64")) Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
        else if (Kind == TEXT("byte") || Kind == TEXT("enum")) Type.PinCategory = UEdGraphSchema_K2::PC_Byte;
        else if (Kind == TEXT("float") || Kind == TEXT("double")) { Type.PinCategory = UEdGraphSchema_K2::PC_Real; Type.PinSubCategory = Kind == TEXT("float") ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Double; }
        else if (Kind == TEXT("string")) Type.PinCategory = UEdGraphSchema_K2::PC_String;
        else if (Kind == TEXT("name")) Type.PinCategory = UEdGraphSchema_K2::PC_Name;
        else if (Kind == TEXT("text")) Type.PinCategory = UEdGraphSchema_K2::PC_Text;
        else if (Kind == TEXT("struct")) Type.PinCategory = UEdGraphSchema_K2::PC_Struct;
        else if (Kind == TEXT("object")) Type.PinCategory = UEdGraphSchema_K2::PC_Object;
        else if (Kind == TEXT("class")) Type.PinCategory = UEdGraphSchema_K2::PC_Class;
        else if (Kind == TEXT("soft_object")) Type.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
        else if (Kind == TEXT("soft_class")) Type.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
        else if (Kind == TEXT("interface")) Type.PinCategory = UEdGraphSchema_K2::PC_Interface;
        else return Reject(TEXT("Unsupported type: ") + Kind);
        if (Kind == TEXT("struct")) Type.PinSubCategoryObject = LoadObject<UScriptStruct>(nullptr, *String(Spec, TEXT("object")));
        else if (Kind == TEXT("enum")) Type.PinSubCategoryObject = LoadObject<UEnum>(nullptr, *String(Spec, TEXT("object")));
        else if (Kind == TEXT("object") || Kind == TEXT("class") || Kind == TEXT("soft_object") || Kind == TEXT("soft_class") || Kind == TEXT("interface"))
            Type.PinSubCategoryObject = LoadObject<UClass>(nullptr, *String(Spec, TEXT("object")));
        if ((Kind == TEXT("struct") || Kind == TEXT("enum") || Kind == TEXT("object") || Kind == TEXT("class") || Kind == TEXT("soft_object") || Kind == TEXT("soft_class") || Kind == TEXT("interface")) && !Type.PinSubCategoryObject.IsValid())
            return Reject(TEXT("Type object not found"));
        if (Kind == TEXT("interface") && !CastChecked<UClass>(Type.PinSubCategoryObject.Get())->HasAnyClassFlags(CLASS_Interface)) return Reject(TEXT("Expected an interface class"));
        const FString Container = String(Spec, TEXT("container"));
        if (Container == TEXT("array")) Type.ContainerType = EPinContainerType::Array;
        else if (Container == TEXT("set")) Type.ContainerType = EPinContainerType::Set;
        else if (Container == TEXT("map"))
        {
            Type.ContainerType = EPinContainerType::Map;
            const FObject* Value = nullptr;
            FEdGraphPinType ValueType;
            if (Spec->TryGetObjectField(TEXT("value_type"), Value)) { if (!ReadType(*Value, ValueType)) return false; }
            else if (Spec->HasField(TEXT("value_object"))) { ValueType.PinCategory = UEdGraphSchema_K2::PC_Struct; ValueType.PinSubCategoryObject = LoadObject<UScriptStruct>(nullptr, *String(Spec, TEXT("value_object"))); if (!ValueType.PinSubCategoryObject.IsValid()) return Reject(TEXT("Map value struct not found")); }
            else return Reject(TEXT("Map requires value_type"));
            if (ValueType.IsContainer() || ValueType.bIsReference) return Reject(TEXT("Nested containers and reference map values are not supported by K2"));
            Type.PinValueType.TerminalCategory = ValueType.PinCategory;
            Type.PinValueType.TerminalSubCategory = ValueType.PinSubCategory;
            Type.PinValueType.TerminalSubCategoryObject = ValueType.PinSubCategoryObject;
        }
        else if (!Container.IsEmpty()) return Reject(TEXT("Unsupported container"));
        if (Spec->HasField(TEXT("reference")))
        {
            bool Ref;
            if (!Spec->HasTypedField<EJson::Boolean>(TEXT("reference")) || !Spec->TryGetBoolField(TEXT("reference"), Ref)) return Reject(TEXT("reference must be boolean"));
            Type.bIsReference = Ref;
        }
        if (Spec->HasField(TEXT("const")))
        {
            bool Const;
            if (!Spec->HasTypedField<EJson::Boolean>(TEXT("const")) || !Spec->TryGetBoolField(TEXT("const"), Const)) return Reject(TEXT("const must be boolean"));
            Type.bIsConst = Const;
        }
        return true;
    }

    bool ValidateValue(const FEdGraphPinType& Type, const FString& Value)
    {
        if (!Type.IsContainer() && Type.PinCategory == UEdGraphSchema_K2::PC_Boolean && Value != TEXT("true") && Value != TEXT("false")) return Reject(TEXT("Boolean defaults must be true or false"));
        TStrongObjectPtr<UScriptStruct> Scope(NewObject<UScriptStruct>(GetTransientPackage()));
        FCompilerResultsLog Log;
        FProperty* Property = FKismetCompilerUtilities::CreatePropertyOnScope(Scope.Get(), TEXT("Value"), Type, Blueprint->SkeletonGeneratedClass, CPF_None, GetDefault<UEdGraphSchema_K2>(), Log);
        if (!Property || Log.NumErrors) { if (Property) delete Property; return Reject(TEXT("Invalid property type (including unhashable set/map key)")); }
        Scope->AddCppProperty(Property);
        Scope->Bind();
        Scope->StaticLink(true);
        FStructOnScope Data(Scope.Get());
        if (!Type.IsContainer() && (Type.PinCategory == UEdGraphSchema_K2::PC_String || Type.PinCategory == UEdGraphSchema_K2::PC_Text)) return true;
        const TCHAR* End = Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<void>(Data.GetStructMemory()), Blueprint, PPF_None);
        if (!End || !FString(End).TrimStartAndEnd().IsEmpty()) return Reject(TEXT("Invalid default value for property type"));
        if (auto* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            if (!CastField<FSoftObjectProperty>(Property) && Value != TEXT("None") && !Value.IsEmpty() && !ObjectProperty->GetObjectPropertyValue_InContainer(Data.GetStructMemory())) return Reject(TEXT("Hard reference default did not resolve to an object"));
        return true;
    }

    bool Variable(const FObject& Op)
    {
        if (Blueprint->BlueprintType != BPTYPE_Normal) return Reject(TEXT("Members require a normal Blueprint"));
        const FString Name = String(Op, TEXT("name"));
        if (!NewName(Name)) return false;
        FEdGraphPinType Type;
        if (!ReadType(Op, Type)) return false;
        if (Type.bIsReference || Type.bIsConst) return Reject(TEXT("Member variables cannot be reference/const parameters"));
        FString Default;
        if (Op->HasField(TEXT("default")) && (!Op->HasTypedField<EJson::String>(TEXT("default")) || !Op->TryGetStringField(TEXT("default"), Default) || !ValidateValue(Type, Default))) return Reject(Error.IsEmpty() ? TEXT("default must be a string") : Error);
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*Name), Type, Default)) return Reject(TEXT("AddMemberVariable failed"));
        if (Op->HasField(TEXT("editable")) && Op->GetBoolField(TEXT("editable"))) FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, FName(*Name), false);
        return true;
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
        // Functions with outputs get a function graph and return node.
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

    #include "WorkbenchAdvancedOperations.inl"

    bool LocalDefault(UK2Node_FunctionEntry* Entry, FBPVariableDescription& Local, const FObject& Op, const TCHAR* Field)
    {
        FString Value;
        if (!Op->HasTypedField<EJson::String>(Field) || !Op->TryGetStringField(Field, Value)) return Reject(TEXT("Local default must be a string"));
        if (!ValidateValue(Local.VarType, Value)) return false;
        Local.DefaultValue = Value;
        Entry->RefreshFunctionVariableCache();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }

    bool Signature(const FObject& Op)
    {
        const FString Name = String(Op, TEXT("name"));
        UEdGraph* Target = nullptr;
        for (UEdGraph* G : Blueprint->FunctionGraphs) if (G->GetName() == Name) Target = G;
        for (UEdGraph* G : Blueprint->DelegateSignatureGraphs) if (G->GetName() == Name) Target = G;
        if (!Target)
        {
            if (!NewName(Name)) return false;
            Target = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(*Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
            FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Target, true, nullptr);
        }
        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* N : Target->Nodes) if (auto* E = Cast<UK2Node_FunctionEntry>(N)) Entry = E;
        if (!Entry) return Reject(TEXT("Missing function entry"));
        const FValues* Parameters = nullptr;
        if (Op->HasField(TEXT("parameters")) && !Op->HasTypedField<EJson::Array>(TEXT("parameters"))) return Reject(TEXT("parameters must be an array"));
        if (Op->TryGetArrayField(TEXT("parameters"), Parameters)) for (const auto& V : *Parameters)
        {
            FObject P = V->AsObject();
            if (!P.IsValid()) return Reject(TEXT("Parameter must be an object"));
            for (const auto& Field : P->Values)
                if (Field.Key != TEXT("name") && Field.Key != TEXT("type") && Field.Key != TEXT("direction") && Field.Key != TEXT("object") && Field.Key != TEXT("default") && Field.Key != TEXT("container") && Field.Key != TEXT("value_type") && Field.Key != TEXT("value_object") && Field.Key != TEXT("reference") && Field.Key != TEXT("const"))
                    return Reject(TEXT("Unsupported parameter field: ") + Field.Key);

            FEdGraphPinType Type;
            if (!ReadType(P, Type)) return false;
            FName PinName(*String(P, TEXT("name")));
            FString Direction = String(P, TEXT("direction"));
            if (Blueprint->DelegateSignatureGraphs.Contains(Target) && (Direction != TEXT("input") || P->HasField(TEXT("default")))) return Reject(TEXT("Dispatcher signatures accept inputs without defaults"));
            if (Direction == TEXT("local"))
            {
                if (Blueprint->BlueprintType == BPTYPE_Interface || Type.bIsReference || Type.bIsConst) return Reject(TEXT("Invalid interface/local reference variable"));
                if (!FBlueprintEditorUtils::AddLocalVariable(Blueprint, Target, PinName, Type)) return Reject(TEXT("Could not add local"));
                if (P->HasField(TEXT("default")))
                    for (FBPVariableDescription& Local : Entry->LocalVariables)
                        if (Local.VarName == PinName && !LocalDefault(Entry, Local, P, TEXT("default"))) return false;
            }
            else if (Direction == TEXT("input"))
            {
                if (Entry->FindPin(PinName)) return Reject(TEXT("Input already exists"));
                Entry->CreateUserDefinedPin(PinName, Type, EGPD_Output);
                if (P->HasField(TEXT("default")))
                {
                    FString Value;
                    if (!P->HasTypedField<EJson::String>(TEXT("default")) || !P->TryGetStringField(TEXT("default"), Value) || !ValidateValue(Type, Value)) return Reject(TEXT("Invalid parameter default"));
                    for (auto& Info : Entry->UserDefinedPins) if (Info->PinName == PinName && !Entry->ModifyUserDefinedPinDefaultValue(Info, Value)) return Reject(TEXT("Parameter default rejected by K2 schema"));
                }
            }
            else if (Direction == TEXT("output"))
            {
                if (P->HasField(TEXT("default"))) return Reject(TEXT("Output defaults are set on return node input pins"));
                TArray<UK2Node_FunctionResult*> Results;
                for (UEdGraphNode* N : Target->Nodes) if (auto* R = Cast<UK2Node_FunctionResult>(N)) Results.Add(R);
                if (Results.IsEmpty()) Results.Add(CastChecked<UK2Node_FunctionResult>(Create<UK2Node_FunctionResult>(Target, [](auto*) {})));
                for (auto* R : Results)
                {
                    if (R->FindPin(PinName)) return Reject(TEXT("Output already exists"));
                    UEdGraphPin* Pin = R->CreateUserDefinedPin(PinName, Type, EGPD_Input);
                    GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
                    for (auto& Info : R->UserDefinedPins) if (Info->PinName == PinName) Info->PinDefaultValue = Pin->DefaultValue;
                }
            }
            else return Reject(TEXT("Invalid parameter direction"));
        }
        for (const TCHAR* Field : {TEXT("entry_id"), TEXT("result_id")})
        {
            if (!Op->HasField(Field)) continue;
            FString Id = String(Op, Field); FGuid Guid;
            if (Id.IsEmpty() || Aliases.Contains(Id) || FGuid::Parse(Id, Guid)) return Reject(TEXT("Signature node alias must be unique and non-Guid"));
            UEdGraphNode* Node = Entry;
            if (FString(Field) == TEXT("result_id"))
            {
                Node = nullptr;
                for (UEdGraphNode* N : Target->Nodes) if (Cast<UK2Node_FunctionResult>(N)) { if (Node) return Reject(TEXT("Multiple results; use inspected GUID")); Node = N; }
            }
            if (!Node) return Reject(TEXT("Signature result node not found"));
            Aliases.Add(Id, Node);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool Component(const FObject& Op)
    {
        UClass* Class = LoadObject<UClass>(nullptr, *String(Op, TEXT("class")));
        FName Name(*String(Op, TEXT("name")));
        if (!Class || !Class->IsChildOf(UActorComponent::StaticClass()) || !Blueprint->SimpleConstructionScript) return Reject(TEXT("Invalid component class/Blueprint"));
        for (USCS_Node* N : Blueprint->SimpleConstructionScript->GetAllNodes()) if (N->GetVariableName() == Name) return Reject(TEXT("Component exists"));
        USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(Class, Name);
        USCS_Node* Root = nullptr;
        for (USCS_Node* N : Blueprint->SimpleConstructionScript->GetRootNodes()) if (N->ComponentClass->IsChildOf(USceneComponent::StaticClass())) { Root = N; break; }
        if (Root && Class->IsChildOf(USceneComponent::StaticClass())) Root->AddChildNode(Node);
        else Blueprint->SimpleConstructionScript->AddNode(Node);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool Node(const FObject& Op, UEdGraph* TargetGraph)
    {
        const FString Id = String(Op, TEXT("id"));
        FGuid Guid;
        if (Id.IsEmpty() || Aliases.Contains(Id) || FGuid::Parse(Id, Guid)) return Reject(TEXT("New node id must be a unique non-Guid alias"));
        const FString Kind = String(Op, TEXT("kind"));
        UEdGraphNode* Added = nullptr;
        if (Kind == TEXT("local_read"))
        {
            UEdGraphNode* Original = ResolveNode(TargetGraph, String(Op, TEXT("source_node")));
            if (!CanLocalizeRead(Original)) return Reject(TEXT("Local read requires an unconnected-context direct member/Self read without getter functions, containers or reference consumers"));
            Added = DuplicateObject<UEdGraphNode>(Original, TargetGraph);
            Added->CreateNewGuid();
            for (UEdGraphPin* Pin : Added->Pins) { Pin->LinkedTo.Empty(); Pin->PinId = FGuid::NewGuid(); }
            TargetGraph->AddNode(Added, false, false);
        }
        else if (Kind == TEXT("branch")) Added = Create<UK2Node_IfThenElse>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("return"))
        {
            UK2Node_FunctionResult* Original = nullptr;
            for (UEdGraphNode* N : TargetGraph->Nodes) if (auto* R = Cast<UK2Node_FunctionResult>(N)) { Original = R; break; }
            if (!Original)
            {
                UK2Node_FunctionEntry* Entry=nullptr;
                for (UEdGraphNode* N:TargetGraph->Nodes) if (auto* E=Cast<UK2Node_FunctionEntry>(N)) Entry=E;
                if (!Entry || !Blueprint->FunctionGraphs.Contains(TargetGraph)) return Reject(TEXT("Return requires a function graph"));
                Added=Create<UK2Node_FunctionResult>(TargetGraph,[Entry](auto* N){N->FunctionReference=Entry->FunctionReference;});
            }
            else
            {
                Added = DuplicateObject<UK2Node_FunctionResult>(Original, TargetGraph);
                Added->CreateNewGuid();
                for (auto* P : Added->Pins) P->LinkedTo.Empty();
                TargetGraph->AddNode(Added, false, false);
            }
        }
        else if (Kind == TEXT("parent_call"))
        {
            UFunction* Function=Blueprint->ParentClass->FindFunctionByName(FName(*String(Op,TEXT("function"))));
            if (!Function) return Reject(TEXT("Parent function missing"));
            Added=Create<UK2Node_CallParentFunction>(TargetGraph,[Function](auto* N){N->SetFromFunction(Function);});
        }
        else if (Kind == TEXT("get_subsystem"))
        {
            UClass* Class=LoadObject<UClass>(nullptr,*String(Op,TEXT("owner")));
            if (!Class || !Class->IsChildOf(UGameInstanceSubsystem::StaticClass())) return Reject(TEXT("GameInstance subsystem class required"));
            Added=Create<UK2Node_GetSubsystem>(TargetGraph,[Class](auto* N){N->Initialize(Class);});
        }
        else if (Kind == TEXT("format_text"))
        {
            auto* N=CastChecked<UK2Node_FormatText>(Create<UK2Node_FormatText>(TargetGraph,[](auto*){}));
            GetDefault<UEdGraphSchema_K2>()->TrySetDefaultText(*N->GetFormatPin(),FText::FromString(String(Op,TEXT("text")))); Added=N;
        }
        else if (Kind == TEXT("create_widget"))
        {
            UClass* Class=LoadObject<UClass>(nullptr,*String(Op,TEXT("owner")));
            if (!Class || !Class->IsChildOf(UUserWidget::StaticClass())) return Reject(TEXT("Widget class required"));
            UClass* NodeClass=LoadObject<UClass>(nullptr,TEXT("/Script/UMGEditor.K2Node_CreateWidget"));
            if (!NodeClass || !NodeClass->IsChildOf(UK2Node_ConstructObjectFromClass::StaticClass())) return Reject(TEXT("CreateWidget node unavailable"));
            auto* N=NewObject<UK2Node_ConstructObjectFromClass>(TargetGraph,NodeClass); TargetGraph->AddNode(N,false,false); N->CreateNewGuid(); N->PostPlacedNewNode(); N->AllocateDefaultPins();
            GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(*N->GetClassPin(),Class);
            N->ReconstructNode(); Added=N;
        }
        else if (Kind == TEXT("self")) Added = Create<UK2Node_Self>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("cast"))
        {
            UClass* Class = LoadObject<UClass>(nullptr, *String(Op, TEXT("owner")));
            if (!Class) return Reject(TEXT("Cast class missing"));
            Added = Create<UK2Node_DynamicCast>(TargetGraph, [Class](auto* N) { N->TargetType = Class; });
        }
        else if (Kind == TEXT("sequence")) Added = Create<UK2Node_ExecutionSequence>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("select")) Added = Create<UK2Node_Select>(TargetGraph, [](auto*) {});
        else if (Kind == TEXT("switch_enum"))
        {
            UEnum* Enum = LoadObject<UEnum>(nullptr, *String(Op, TEXT("enum")));
            if (!Enum) return Reject(TEXT("Enum not found"));
            Added = Create<UK2Node_SwitchEnum>(TargetGraph, [Enum](auto* N) { N->Enum = Enum; });
        }
        else if (Kind == TEXT("set_members_struct"))
        {
            auto* Struct = LoadObject<UScriptStruct>(nullptr, *String(Op, TEXT("struct")));
            if (!Struct) return Reject(TEXT("Struct not found"));
            if (!UK2Node_MakeStruct::CanBeMade(Struct))
                return Reject(TEXT("Struct cannot use native Set Members; use its native Make function and assign the result"));
            auto* N = CastChecked<UK2Node_SetFieldsInStruct>(Create<UK2Node_SetFieldsInStruct>(TargetGraph, [Struct](auto* N) { N->StructType = Struct; }));
            N->RestoreAllPins();
            const FValues* Fields = nullptr;
            if (Op->TryGetArrayField(TEXT("fields"), Fields))
            {
                TSet<FName> Names;
                for (const auto& V : *Fields) { FString Name; if (!V->TryGetString(Name) || !N->FindPin(FName(*Name))) return Reject(TEXT("Struct member pin not found")); Names.Add(FName(*Name)); }
                for (auto& Pin : N->ShowPinForProperties) Pin.bShowPin = Names.Contains(Pin.PropertyName);
                N->ReconstructNode();
            }
            Added = N;
        }
        else if (Kind == TEXT("interface_event")) { if (!InterfaceEvent(Op, TargetGraph, Added)) return false; }
        else if (Kind == TEXT("interface_message"))
        {
            UClass* Owner = LoadObject<UClass>(nullptr, *String(Op, TEXT("owner")));
            UFunction* Function = Owner ? Owner->FindFunctionByName(FName(*String(Op, TEXT("function")))) : nullptr;
            if (!Owner || !Owner->HasAnyClassFlags(CLASS_Interface) || !Function) return Reject(TEXT("Interface function not found"));
            Added = Create<UK2Node_Message>(TargetGraph, [Function](auto* N) { N->SetFromFunction(Function); });
        }
        else if (Kind == TEXT("dispatcher_call"))
        {
            UClass* Owner = String(Op,TEXT("owner")).IsEmpty() ? Blueprint->SkeletonGeneratedClass.Get() : LoadObject<UClass>(nullptr, *String(Op,TEXT("owner")));
            auto* Property = Owner ? FindFProperty<FMulticastDelegateProperty>(Owner, FName(*String(Op,TEXT("name")))) : nullptr;
            if (!Property) return Reject(TEXT("Dispatcher not found"));
            Added = Create<UK2Node_CallDelegate>(TargetGraph, [Property,Owner](auto* N) { N->SetFromProperty(Property, false, Owner); });
        }
        else if (Kind == TEXT("make_array"))
        {
            double Count = 1;
            if (Op->HasField(TEXT("num_inputs")) && (!Op->HasTypedField<EJson::Number>(TEXT("num_inputs")) || !Op->TryGetNumberField(TEXT("num_inputs"), Count) ||
                !FMath::IsFinite(Count) || Count < 0 || Count > 1024 || Count != FMath::FloorToDouble(Count)))
                return Reject(TEXT("num_inputs must be an integer between 0 and 1024"));
            Added = Create<UK2Node_MakeArray>(TargetGraph, [Count](auto* N) { N->NumInputs = static_cast<int32>(Count); });
        }
        else if (Kind == TEXT("make_struct"))
        {
            UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *String(Op, TEXT("struct")));
            if (!Struct) return Reject(TEXT("Struct missing"));
            Added = Create<UK2Node_MakeStruct>(TargetGraph, [Struct](auto* N) { N->StructType = Struct; });
        }
        else if (Kind == TEXT("delegate"))
            Added = Create<UK2Node_CreateDelegate>(TargetGraph, [&Op](auto* N) { N->SetFunction(FName(*String(Op, TEXT("function")))); });
        else if (Kind == TEXT("bind") || Kind == TEXT("unbind"))
        {
            UClass* Owner = LoadObject<UClass>(nullptr, *String(Op, TEXT("owner")));
            FName Name(*String(Op, TEXT("name")));
            if (!Owner || !FindFProperty<FMulticastDelegateProperty>(Owner, Name)) return Reject(TEXT("Delegate property missing"));
            auto Configure = [Owner,Name](auto* N) { N->SetFromProperty(FindFProperty<FMulticastDelegateProperty>(Owner, Name), false, Owner); };
            Added = Kind == TEXT("bind") ? Create<UK2Node_AddDelegate>(TargetGraph, Configure) : Create<UK2Node_RemoveDelegate>(TargetGraph, Configure);
        }
        else if (Kind == TEXT("component_event"))
        {
            auto* Property = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, FName(*String(Op,TEXT("variable"))));
            auto* Delegate = Property ? FindFProperty<FMulticastDelegateProperty>(Property->PropertyClass,FName(*String(Op,TEXT("name")))) : nullptr;
            if (!Property || !Delegate) return Reject(TEXT("Component event property missing"));
            Added = Create<UK2Node_ComponentBoundEvent>(TargetGraph, [Property,Delegate](auto* N) { N->InitializeComponentBoundEventParams(Property,Delegate); });
        }
        else if (Kind == TEXT("foreach") || Kind == TEXT("for_loop") || Kind == TEXT("for_loop_break") || Kind == TEXT("foreach_break"))
        {
            auto* MacroBP = LoadObject<UBlueprint>(nullptr,TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
            UEdGraph* Macro = nullptr;
            const FString MacroName = Kind == TEXT("foreach") ? TEXT("ForEachLoop") : Kind == TEXT("for_loop") ? TEXT("ForLoop") : Kind == TEXT("for_loop_break") ? TEXT("ForLoopWithBreak") : TEXT("ForEachLoopWithBreak");
            if (MacroBP) for (UEdGraph* G : MacroBP->MacroGraphs) if (G->GetName() == MacroName) Macro = G;
            if (!Macro) return Reject(TEXT("ForEach macro missing"));
            Added = Create<UK2Node_MacroInstance>(TargetGraph, [Macro](auto* N) { N->SetMacroGraph(Macro); });
        }
        else if (Kind == TEXT("comment"))
        {
            double Width = 0, Height = 0;
            if (!Op->TryGetNumberField(TEXT("width"), Width) || !Op->TryGetNumberField(TEXT("height"), Height) ||
                !FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width < 100 || Height < 100 || Width > 100000 || Height > 100000)
                return Reject(TEXT("Comment requires finite width/height between 100 and 100000"));
            if (String(Op, TEXT("text")).IsEmpty()) return Reject(TEXT("Comment text is required"));
            UEdGraphNode_Comment* Comment = CastChecked<UEdGraphNode_Comment>(Create<UEdGraphNode_Comment>(TargetGraph, [](auto*) {}));
            Comment->NodeComment = String(Op, TEXT("text"));
            SetCommentDetails(Comment, String(Op, TEXT("details")));
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
        else if (Kind == TEXT("call") || Kind == TEXT("container_call"))
        {
            const FString OwnerPath = String(Op, TEXT("owner"));
            UClass* Owner = OwnerPath.IsEmpty() ? Blueprint->SkeletonGeneratedClass.Get() : LoadObject<UClass>(nullptr, *OwnerPath);
            UFunction* Function = Owner ? Owner->FindFunctionByName(FName(*String(Op, TEXT("function")))) : nullptr;
            if (!Function || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
                return Reject(TEXT("Blueprint callable function not found"));
            if (Kind != TEXT("container_call") && (Function->HasMetaData(TEXT("BlueprintInternalUseOnly")) || Function->HasMetaData(TEXT("CustomStructureParam")) || Function->HasMetaData(TEXT("ArrayParm"))))
                return Reject(TEXT("This function needs a specialized node; generic calls are not supported"));
            if (Function->HasMetaData(TEXT("Latent")) && !Blueprint->UbergraphPages.Contains(TargetGraph))
                return Reject(TEXT("Latent call requires an event graph"));
            if (Function->HasMetaData(TEXT("ArrayParm"))) Added = Create<UK2Node_CallArrayFunction>(TargetGraph, [Function](auto* N) { N->SetFromFunction(Function); });
            else Added = Create<UK2Node_CallFunction>(TargetGraph, [Function](auto* NewNode) { NewNode->SetFromFunction(Function); });
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
        else if (Kind == TEXT("local_get") || Kind == TEXT("local_set"))
        {
            FName Name(*String(Op,TEXT("variable")));
            auto Configure = [Name,TargetGraph](auto* N) { N->VariableReference.SetLocalMember(Name, TargetGraph->GetName(), FGuid()); };
            Added = Kind == TEXT("local_get") ? Create<UK2Node_VariableGet>(TargetGraph, Configure) : Create<UK2Node_VariableSet>(TargetGraph, Configure);
        }
        else if (Kind == TEXT("custom_event"))
        {
            const FString Name = String(Op, TEXT("name"));
            if (!Blueprint->UbergraphPages.Contains(TargetGraph)) return Reject(TEXT("Custom events require an event graph"));
            if (!NewName(Name)) return false;
            Added = Create<UK2Node_CustomEvent>(TargetGraph, [&Name](auto* NewNode) { NewNode->CustomFunctionName = FName(*Name); });
        }
        else return Reject(TEXT("Unsupported node kind: ") + Kind);
        if (auto* Event = Cast<UK2Node_CustomEvent>(Added))
            if (!EventParameters(Event, Op)) return false;
        double X = 0, Y = 0;
        Op->TryGetNumberField(TEXT("x"), X);
        Op->TryGetNumberField(TEXT("y"), Y);
        Added->NodePosX = FMath::Clamp(X, -1000000.0, 1000000.0);
        Added->NodePosY = FMath::Clamp(Y, -1000000.0, 1000000.0);
        Aliases.Add(Id, Added);
        return true;
    }

    bool WidgetEdit(const FObject& Op)
    {
        auto* BP = Cast<UWidgetBlueprint>(Blueprint);
        if (!BP || !BP->WidgetTree) return Reject(TEXT("Widget Blueprint required"));
        const FString Kind = String(Op, TEXT("op"));
        const FName Name(*String(Op, TEXT("name")));
        if (Kind == TEXT("add_widget"))
        {
            UClass* Class = LoadObject<UClass>(nullptr, *String(Op,TEXT("class")));
            if (Name.IsNone() || BP->WidgetTree->FindWidget(Name) || !Class || !Class->IsChildOf(UWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract)) return Reject(TEXT("Invalid or duplicate widget"));
            UPanelWidget* Parent = nullptr;
            if (Op->HasField(TEXT("parent")))
            {
                Parent = Cast<UPanelWidget>(BP->WidgetTree->FindWidget(FName(*String(Op,TEXT("parent")))));
                if (!Parent || (!Parent->CanHaveMultipleChildren() && Parent->GetChildrenCount())) return Reject(TEXT("Parent missing or single-child container occupied"));
            }
            else if (BP->WidgetTree->RootWidget) return Reject(TEXT("Root already exists"));
            UWidget* Widget = BP->WidgetTree->ConstructWidget<UWidget>(Class, Name);
            Widget->bIsVariable = true;
            if (Parent) { if (!Parent->AddChild(Widget)) return Reject(TEXT("Could not attach widget")); }
            else BP->WidgetTree->RootWidget = Widget;
        }
        else
        {
            UWidget* Widget = BP->WidgetTree->FindWidget(Name);
            UObject* Target = Widget;
            bool Slot = false; Op->TryGetBoolField(TEXT("slot"), Slot);
            if (Slot) Target = Widget ? Widget->Slot : nullptr;
            FProperty* Property = Target ? FindFProperty<FProperty>(Target->GetClass(), FName(*String(Op,TEXT("property")))) : nullptr;
            if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst)) return Reject(TEXT("Editable widget property not found"));
            const FString Value=String(Op,TEXT("value"));
            if (!Property->ImportText_Direct(*Value, Property->ContainerPtrToValuePtr<void>(Target), Target, PPF_None)) return Reject(TEXT("Invalid widget property value"));
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool Apply(const FObject& Op)
    {
        const FString Kind = String(Op, TEXT("op"));
        if (Blueprint->BlueprintType == BPTYPE_Interface && Kind != TEXT("signature") && Kind != TEXT("function_properties") && Kind != TEXT("rename_function") && Kind != TEXT("edit_parameter"))
            return Reject(TEXT("Interfaces permit only signature, function metadata and parameter edits"));
        // Catch misspelled/unsupported fields rather than silently applying a different patch.
        TArray<FString> Fields;
        if (Kind == TEXT("set_delegate_function")) Fields = {TEXT("op"),TEXT("graph"),TEXT("node"),TEXT("function")};
        else if (Kind == TEXT("add_widget")) Fields = {TEXT("op"),TEXT("name"),TEXT("class"),TEXT("parent")};
        else if (Kind == TEXT("set_widget_property")) Fields = {TEXT("op"),TEXT("name"),TEXT("property"),TEXT("value"),TEXT("slot")};
        else if (Kind == TEXT("variable_metadata")) Fields = {TEXT("op"),TEXT("name"),TEXT("tooltip"),TEXT("expose_on_spawn"),TEXT("private")};
        else if (Kind == TEXT("signature")) Fields = {TEXT("op"), TEXT("name"), TEXT("parameters"), TEXT("entry_id"), TEXT("result_id")};
        else if (Kind == TEXT("function_properties")) Fields = {TEXT("op"), TEXT("graph"), TEXT("pure"), TEXT("access"), TEXT("category"), TEXT("tooltip")};
        else if (Kind == TEXT("set_member_default")) Fields = {TEXT("op"), TEXT("variable"), TEXT("value")};
        else if (Kind == TEXT("add_dispatcher")) Fields = {TEXT("op"), TEXT("name"), TEXT("parameters")};
        else if (Kind == TEXT("refresh_interfaces")) Fields = {TEXT("op")};
        else if (Kind == TEXT("implement_interface")) Fields = {TEXT("op"), TEXT("interface")};
        else if (Kind == TEXT("rename_function") || Kind == TEXT("rename_variable")) Fields = {TEXT("op"), TEXT("name"), TEXT("new_name"), TEXT("graph")};
        else if (Kind == TEXT("edit_parameter")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("name"), TEXT("direction"), TEXT("action"), TEXT("new_name"), TEXT("value"), TEXT("type"), TEXT("object"), TEXT("container"), TEXT("value_type"), TEXT("reference"), TEXT("const")};
        else if (Kind == TEXT("edit_pins")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("pin"), TEXT("action")};
        else if (Kind == TEXT("replace_node")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("replacement"), TEXT("pin_map")};
        else if (Kind == TEXT("add_component")) Fields = {TEXT("op"), TEXT("name"), TEXT("class")};
        else if (Kind == TEXT("refresh_calls")) Fields = {TEXT("op"), TEXT("functions"), TEXT("owner")};
        else if (Kind == TEXT("add_variable")) Fields = {TEXT("op"), TEXT("name"), TEXT("type"), TEXT("object"), TEXT("container"), TEXT("value_object"), TEXT("editable"), TEXT("value_type"), TEXT("default"), TEXT("reference"), TEXT("const")};
        else if (Kind == TEXT("add_override")) Fields = {TEXT("op"), TEXT("name"), TEXT("graph")};
        else if (Kind == TEXT("add_node")) Fields = {TEXT("op"), TEXT("graph"), TEXT("kind"), TEXT("id"), TEXT("x"), TEXT("y"), TEXT("owner"), TEXT("function"), TEXT("variable"), TEXT("name"), TEXT("struct"), TEXT("text"), TEXT("details"), TEXT("width"), TEXT("height"), TEXT("members"), TEXT("source_node"), TEXT("num_inputs"), TEXT("enum"), TEXT("fields"), TEXT("parameters")};
        else if (Kind == TEXT("move_node")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("x"), TEXT("y")};
        else if (Kind == TEXT("remove_unused_nodes")) Fields = {TEXT("op"), TEXT("graph"), TEXT("nodes")};
        else if (Kind == TEXT("set_local_default")) Fields = {TEXT("op"), TEXT("graph"), TEXT("variable"), TEXT("value")};
        else if (Kind == TEXT("set_call_in_editor")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("value")};
        else if (Kind == TEXT("set_default")) Fields = {TEXT("op"), TEXT("graph"), TEXT("node"), TEXT("pin"), TEXT("pin_id"), TEXT("value")};
        else if (Kind == TEXT("connect") || Kind == TEXT("disconnect")) Fields = {TEXT("op"), TEXT("graph"), TEXT("from"), TEXT("to")};
        else return Reject(TEXT("Unsupported operation: ") + Kind);
        for (const auto& Field : Op->Values)
            if (!Fields.Contains(Field.Key)) return Reject(TEXT("Unsupported field: ") + Field.Key);
        if (Kind == TEXT("set_delegate_function"))
        {
            UEdGraph* G=Graph(String(Op,TEXT("graph"))); if (!G) return false;
            auto* N=Cast<UK2Node_CreateDelegate>(ResolveNode(G,String(Op,TEXT("node"))));
            if (!N) return Reject(TEXT("CreateDelegate node required")); N->SetFunction(FName(*String(Op,TEXT("function")))); return true;
        }
        if (Kind == TEXT("add_widget") || Kind == TEXT("set_widget_property")) return WidgetEdit(Op);
        if (Kind == TEXT("variable_metadata"))
        {
            const FName Name(*String(Op,TEXT("name")));
            if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint,Name)==INDEX_NONE) return Reject(TEXT("Owned variable not found"));
            if (Op->HasField(TEXT("private"))) { FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint,Name,nullptr,TEXT("Private")); if (Op->GetBoolField(TEXT("private"))) FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint,Name,nullptr,FBlueprintMetadata::MD_Private,TEXT("true")); else FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(Blueprint,Name,nullptr,FBlueprintMetadata::MD_Private); }
            if (Op->HasField(TEXT("tooltip"))) FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint,Name,nullptr,TEXT("tooltip"),String(Op,TEXT("tooltip")));
            if (Op->HasField(TEXT("expose_on_spawn"))) FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint,Name,nullptr,TEXT("ExposeOnSpawn"),Op->GetBoolField(TEXT("expose_on_spawn")) ? TEXT("true") : TEXT("false"));
            return true;
        }
        if (Kind == TEXT("function_properties")) return FunctionProperties(Op);
        if (Kind == TEXT("set_member_default")) return MemberDefault(Op);
        if (Kind == TEXT("add_dispatcher")) return Dispatcher(Op);
        if (Kind == TEXT("implement_interface")) return ImplementInterface(Op);
        if (Kind == TEXT("refresh_interfaces"))
        {
            if (Blueprint->BlueprintType != BPTYPE_Normal) return Reject(TEXT("Interface implementations require a normal Blueprint"));
            FBlueprintEditorUtils::ConformImplementedInterfaces(Blueprint);
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            return true;
        }
        if (Kind == TEXT("rename_function") || Kind == TEXT("rename_variable")) return Rename(Op);
        if (Kind == TEXT("edit_parameter")) return EditParameter(Op);
        if (Kind == TEXT("signature")) return Signature(Op);
        if (Kind == TEXT("add_component")) return Component(Op);
        if (Kind == TEXT("refresh_calls"))
        {
            const FValues* Functions = nullptr;
            if (!Op->TryGetArrayField(TEXT("functions"), Functions) || Functions->IsEmpty())
                return Reject(TEXT("Explicit nonempty function list required"));
            UClass* Owner = Op->HasField(TEXT("owner")) ? LoadObject<UClass>(nullptr, *String(Op,TEXT("owner"))) : nullptr;
            if (Op->HasField(TEXT("owner")) && !Owner) return Reject(TEXT("Refresh owner not found"));
            TSet<FName> Names;
            for (const auto& Value : *Functions)
            {
                FString Name;
                if (!Value->TryGetString(Name) || Name.IsEmpty()) return Reject(TEXT("Function name must be a nonempty string"));
                Names.Add(FName(*Name));
            }
            TArray<UEdGraph*> Graphs;
            Blueprint->GetAllGraphs(Graphs);
            for (UEdGraph* G : Graphs)
                for (UEdGraphNode* N : G->Nodes)
                {
                    const FMemberReference* Reference = nullptr;
                    if (auto* Call = Cast<UK2Node_CallFunction>(N)) Reference = &Call->FunctionReference;
                    if (auto* Delegate = Cast<UK2Node_BaseMCDelegate>(N)) Reference = &Delegate->DelegateReference;
                    if (Reference && Names.Contains(Reference->GetMemberName()) && (!Owner || Reference->GetMemberParentClass(Blueprint->SkeletonGeneratedClass) == Owner)) N->ReconstructNode();
                }
            return true;
        }
        if (Kind == TEXT("add_variable")) return Variable(Op);
        if (Kind == TEXT("add_override")) return Override(Op);
        UEdGraph* TargetGraph = Graph(String(Op, TEXT("graph")));
        if (!TargetGraph) return false;
        if (Kind == TEXT("edit_pins")) return PinEdit(Op, TargetGraph);
        if (Kind == TEXT("replace_node")) return ReplaceNode(Op, TargetGraph);
        if (Kind == TEXT("set_local_default"))
        {
            for (UEdGraphNode* N : TargetGraph->Nodes)
                if (auto* Entry = Cast<UK2Node_FunctionEntry>(N))
                    for (FBPVariableDescription& Local : Entry->LocalVariables)
                        if (Local.VarName == FName(*String(Op, TEXT("variable"))))
                            return LocalDefault(Entry, Local, Op, TEXT("value"));
            return Reject(TEXT("Local variable not found in function graph"));
        }
        if (Kind == TEXT("set_call_in_editor"))
        {
            bool Value = false;
            if (!Op->HasTypedField<EJson::Boolean>(TEXT("value")) || !Op->TryGetBoolField(TEXT("value"), Value)) return Reject(TEXT("value must be a boolean"));
            UEdGraphNode* Target = nullptr;
            if (Op->HasField(TEXT("node"))) Target = ResolveNode(TargetGraph, String(Op, TEXT("node")));
            else for (UEdGraphNode* N : TargetGraph->Nodes) if (Cast<UK2Node_FunctionEntry>(N)) { Target = N; break; }
            if (auto* Entry = Cast<UK2Node_FunctionEntry>(Target)) Entry->MetaData.bCallInEditor = Value;
            else if (auto* Event = Cast<UK2Node_CustomEvent>(Target)) Event->bCallInEditor = Value;
            else return Reject(TEXT("Call in Editor requires a function entry or custom event"));
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            return true;
        }
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
                    Cast<UK2Node_Knot>(Existing) || Cast<UK2Node_BreakStruct>(Existing) || Cast<UK2Node_GetEnumeratorNameAsString>(Existing) || Cast<UK2Node_Self>(Existing) || Cast<UK2Node_MakeStruct>(Existing) || Cast<UK2Node_Select>(Existing) || Cast<UK2Node_EnumEquality>(Existing) || Cast<UK2Node_SwitchInteger>(Existing) || Cast<UK2Node_CallDelegate>(Existing) || Cast<UK2Node_CreateDelegate>(Existing) || Cast<UK2Node_SpawnActorFromClass>(Existing) || Existing->GetClass()->GetPathName() == TEXT("/Script/UMGEditor.K2Node_CreateWidget");
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
                            Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate))
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
            if (!Op->HasTypedField<EJson::String>(TEXT("value")) || !Op->TryGetStringField(TEXT("value"), Value)) return Reject(TEXT("value must be a string"));
            if (!Input->LinkedTo.IsEmpty() || Input->bDefaultValueIsIgnored || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                return Reject(TEXT("Default requires an unconnected data input"));
            if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Object || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Class || Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
            {
                UObject* Object = Value.IsEmpty() || Value == TEXT("None") ? nullptr : LoadObject<UObject>(nullptr, *Value);
                if (!Object && !Value.IsEmpty() && Value != TEXT("None")) return Reject(TEXT("Default object not found"));
                const FString Invalid = Schema->IsPinDefaultValid(Input, FString(), Object, FText());
                if (!Invalid.IsEmpty()) return Reject(Invalid);
                Schema->TrySetDefaultObject(*Input, Object); return true;
            }
            if (Input->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
            {
                Schema->TrySetDefaultText(*Input, FText::FromString(Value)); return true;
            }
            // Scalar and soft-reference inputs use the schema string setter.
            const FName Category = Input->PinType.PinCategory;
            // K2 accepts arbitrary bool text as false; reject typos at this external-input boundary.
            if (Category == UEdGraphSchema_K2::PC_Boolean && Value != TEXT("true") && Value != TEXT("false"))
                return Reject(TEXT("Boolean defaults must be true or false"));
            if (Category != UEdGraphSchema_K2::PC_Boolean && Category != UEdGraphSchema_K2::PC_Int &&
                Category != UEdGraphSchema_K2::PC_Real && Category != UEdGraphSchema_K2::PC_String &&
                Category != UEdGraphSchema_K2::PC_Name && Category != UEdGraphSchema_K2::PC_Struct && Category != UEdGraphSchema_K2::PC_Byte && Category != UEdGraphSchema_K2::PC_Int64 && Category != UEdGraphSchema_K2::PC_SoftObject && Category != UEdGraphSchema_K2::PC_SoftClass)
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

static FString ApplyPatchInternal(const FString& AssetPath, const FString& PatchJson, const FString& DestinationPackage, bool bWriteSource, bool bPreviewSource = false)
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
    if ((Source->GetClass() != UBlueprint::StaticClass() && Source->GetClass() != UWidgetBlueprint::StaticClass()) || (Source->BlueprintType != BPTYPE_Normal && Source->BlueprintType != BPTYPE_FunctionLibrary && Source->BlueprintType != BPTYPE_Interface))
        return Fail(Out, TEXT("Supported assets: normal Blueprint, Widget, function library and interface"));
    const bool bUseSource = bWriteSource || bPreviewSource;
    if (bPreviewSource && (!IsRunningCommandlet() || !Source->GetOutermost()->GetName().StartsWith(TEXT("/Game/")))) return Fail(Out,TEXT("Identity preview requires an isolated commandlet and /Game asset"));
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
    const FString PackageName = bUseSource ? Source->GetOutermost()->GetName() :
        (bSaveCopy ? DestinationPackage : TEXT("/Temp/BlueprintWorkbench_") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    UPackage* Package = bUseSource ? Source->GetOutermost() : CreatePackage(*PackageName);
    TStrongObjectPtr<UPackage> KeepPackage(Package);
    if (!bSave && !bUseSource) Package->SetFlags(RF_Transient);
    TMap<UObject*, UObject*> DuplicatedObjects;
    FObjectDuplicationParameters Duplication(Source, Package);
    Duplication.DestName = FName(*FPackageName::GetLongPackageAssetName(PackageName));
    Duplication.CreatedObjects = &DuplicatedObjects;
    UBlueprint* Copy = bUseSource ? Source : Cast<UBlueprint>(StaticDuplicateObjectEx(Duplication));
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
        {
            Context.SourceNodes.Add(SourceNode->GetGraph()->GetPathName(Source) + TEXT("|") + SourceNode->NodeGuid.ToString(), CopyNode);
            // Event PostDuplicate can rebind an interface override to the new self class.
            // Keep the declared external contract, not the temporary generated implementation.
            if (auto* SourceEvent = Cast<UK2Node_Event>(SourceNode))
                if (SourceEvent->bOverrideFunction)
                    if (auto* CopyEvent = Cast<UK2Node_Event>(CopyNode))
                    {
                        CopyEvent->EventReference = SourceEvent->EventReference;
                        CopyEvent->UpdateDelegatePin(true);
                    }
            // Package metadata is not part of StaticDuplicateObjectEx's object property copy.
            if (auto* SourceComment = Cast<UEdGraphNode_Comment>(SourceNode))
                if (!FindFProperty<FTextProperty>(SourceComment->GetClass(), TEXT("NodeDetails")))
                    SetCommentDetails(CastChecked<UEdGraphNode_Comment>(CopyNode), CommentDetails(SourceComment));
        }
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
                if (bSaved)
                {
                    // Saving can assign persistent FText identities; report the post-save state.
                    Out->SetObjectField(TEXT("blueprint"), Describe(Copy));
                    if (!bWriteSource) FAssetRegistryModule::AssetCreated(Copy);
                    // Make immediate cross-asset impact queries observe this saved package.
                    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanFilesSynchronous({Filename}, true);
                }
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
    if (!bUseSource && (!bSave || !Out->GetBoolField(TEXT("saved"))))
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

FString UBlueprintWorkbenchLibrary::CreateBlueprint(const FString& Kind, const FString& ParentClass, const FString& PatchJson, const FString& DestinationPackage)
{
    using namespace BlueprintWorkbench;
    if (!IsInGameThread()) return Fail(Result(), TEXT("Must run on the editor game thread"));
    EBlueprintType Type = BPTYPE_Normal;
    UClass* Parent = nullptr;
    if (Kind == TEXT("function_library")) { Type = BPTYPE_FunctionLibrary; Parent = UBlueprintFunctionLibrary::StaticClass(); }
    else if (Kind == TEXT("interface")) { Type = BPTYPE_Interface; Parent = UInterface::StaticClass(); }
    else if (Kind == TEXT("widget")) Parent = UUserWidget::StaticClass();
    else if (Kind == TEXT("normal")) Parent = LoadObject<UClass>(nullptr, *ParentClass);
    else return Fail(Result(), TEXT("kind must be normal, function_library or interface"));
    if (!Parent || (Type == BPTYPE_Normal && !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))) return Fail(Result(), TEXT("Invalid Blueprint parent"));
    const FString SeedName = TEXT("/Temp/WorkbenchSeed_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package = CreatePackage(*SeedName);
    Package->SetFlags(RF_Transient);
    TStrongObjectPtr<UBlueprint> Seed(FKismetEditorUtilities::CreateBlueprint(Parent, Package, *FPackageName::GetLongPackageAssetName(SeedName), Type, Kind == TEXT("widget") ? UWidgetBlueprint::StaticClass() : UBlueprint::StaticClass(), Kind == TEXT("widget") ? UWidgetBlueprintGeneratedClass::StaticClass() : UBlueprintGeneratedClass::StaticClass()));
    if (!Seed.IsValid()) return Fail(Result(), TEXT("Could not create Blueprint"));
    const FString ResultJson = ApplyPatchInternal(Seed->GetPathName(), PatchJson, DestinationPackage, false);
    Seed->ClearFlags(RF_Public | RF_Standalone); Package->SetDirtyFlag(false);
    return ResultJson;
}

FString UBlueprintWorkbenchLibrary::FindMemberReferences(const FString& AssetPath, const FString& MemberName)
{
    using namespace BlueprintWorkbench;
    FObject Out = Result();
    if (!IsInGameThread()) return Fail(Out, TEXT("Must run on editor game thread"));
    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP || !BP->GeneratedClass || MemberName.IsEmpty()) return Fail(Out, TEXT("Blueprint and member name required"));
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    Registry.SearchAllAssets(true);
    TArray<FName> Packages; Registry.GetReferencers(BP->GetOutermost()->GetFName(), Packages);
    Packages.AddUnique(BP->GetOutermost()->GetFName());
    FValues Candidates, References;
    for (const FName Package : Packages)
    {
        Candidates.Add(MakeShared<FJsonValueString>(Package.ToString()));
        UBlueprint* Candidate = LoadObject<UBlueprint>(nullptr, *(Package.ToString() + TEXT(".") + FPackageName::GetLongPackageAssetName(Package.ToString())));
        if (!Candidate) continue;
        TArray<UEdGraph*> Graphs; Candidate->GetAllGraphs(Graphs);
        for (UEdGraph* G : Graphs) for (UEdGraphNode* N : G->Nodes)
        {
            const FMemberReference* Ref = nullptr;
            if (auto* Call = Cast<UK2Node_CallFunction>(N)) Ref = &Call->FunctionReference;
            if (auto* Var = Cast<UK2Node_Variable>(N)) Ref = &Var->VariableReference;
            if (auto* Delegate = Cast<UK2Node_BaseMCDelegate>(N)) Ref = &Delegate->DelegateReference;
            if (auto* Event = Cast<UK2Node_Event>(N)) Ref = &Event->EventReference;
            if (!Ref || Ref->GetMemberName() != FName(*MemberName)) continue;
            UClass* Owner = Ref->GetMemberParentClass(Candidate->GeneratedClass);
            if (Owner != BP->GeneratedClass && Owner != BP->SkeletonGeneratedClass && !(Candidate == BP && Ref->IsSelfContext())) continue;
            FObject Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("asset"), Candidate->GetPathName()); Item->SetStringField(TEXT("graph"), G->GetPathName(Candidate)); Item->SetStringField(TEXT("node"), N->NodeGuid.ToString()); References.Add(MakeShared<FJsonValueObject>(Item));
        }
    }
    Out->SetArrayField(TEXT("candidate_packages"), Candidates); Out->SetArrayField(TEXT("references"), References);
    Out->SetStringField(TEXT("scope"), TEXT("Saved asset-registry package referencers and direct function/variable K2 references; excludes unsaved editors, soft-name references and non-Blueprint assets"));
    Out->SetBoolField(TEXT("success"), true); return Json(Out);
}

#include "WorkbenchSchemaAssets.inl"


FString UBlueprintWorkbenchLibrary::ApplyPatchInIsolatedCommandlet(const FString& AssetPath, const FString& PatchJson, bool bWriteBack)
{
    using namespace BlueprintWorkbench;
    if (!IsRunningCommandlet()) return Fail(Result(),TEXT("Isolated commandlet required; close editor first"));
    return ApplyPatchInternal(AssetPath,PatchJson,TEXT(""),bWriteBack,!bWriteBack);
}
