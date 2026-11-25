// Included inside FPatch: operations share the existing patch ownership and failure contract.
    UK2Node_FunctionEntry* EntryFor(UEdGraph* G)
    {
        for (UEdGraphNode* N : G->Nodes) if (auto* E = Cast<UK2Node_FunctionEntry>(N)) return E;
        Reject(TEXT("Function entry not found"));
        return nullptr;
    }

    bool FunctionProperties(const FObject& Op)
    {
        UEdGraph* G = Graph(String(Op, TEXT("graph")));
        if (!G) return false;
        auto* Entry = EntryFor(G);
        if (!Entry) return false;
        if (Op->HasField(TEXT("pure")))
        {
            bool Pure;
            if (!Op->HasTypedField<EJson::Boolean>(TEXT("pure")) || !Op->TryGetBoolField(TEXT("pure"), Pure)) return Reject(TEXT("pure must be boolean"));
            if (Pure) Entry->AddExtraFlags(FUNC_BlueprintPure); else Entry->ClearExtraFlags(FUNC_BlueprintPure);
        }
        if (Op->HasField(TEXT("access")))
        {
            FString Access = String(Op, TEXT("access"));
            int32 Flag = Access == TEXT("public") ? FUNC_Public : Access == TEXT("protected") ? FUNC_Protected : Access == TEXT("private") ? FUNC_Private : 0;
            if (!Flag || (Blueprint->BlueprintType == BPTYPE_Interface && Flag != FUNC_Public)) return Reject(TEXT("Invalid function access"));
            Entry->ClearExtraFlags(FUNC_Public | FUNC_Protected | FUNC_Private);
            Entry->AddExtraFlags(Flag);
        }
        if (Op->HasField(TEXT("category"))) Entry->MetaData.Category = FText::FromString(String(Op, TEXT("category")));
        if (Op->HasField(TEXT("tooltip"))) Entry->MetaData.ToolTip = FText::FromString(String(Op, TEXT("tooltip")));
        Entry->ReconstructNode();
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool MemberDefault(const FObject& Op)
    {
        if (Blueprint->BlueprintType != BPTYPE_Normal) return Reject(TEXT("Class defaults require a normal Blueprint"));
        FString Value;
        if (!Op->HasTypedField<EJson::String>(TEXT("value")) || !Op->TryGetStringField(TEXT("value"), Value)) return Reject(TEXT("value must be an Unreal property text string"));
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        if (Log.NumErrors || !Blueprint->GeneratedClass) return Reject(TEXT("Blueprint must compile before editing class defaults"));
        FProperty* Property = FindFProperty<FProperty>(Blueprint->GeneratedClass, FName(*String(Op, TEXT("variable"))));
        if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible) || Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnTemplate)) return Reject(TEXT("Property is not editable on class defaults"));
        FEdGraphPinType Type;
        if (!GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, Type) || !ValidateValue(Type, Value)) return false;
        UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
        if (!FBlueprintEditorUtils::PropertyValueFromString(Property, Value, reinterpret_cast<uint8*>(CDO), CDO)) return Reject(TEXT("Could not import class default"));
        FString Export;
        FBlueprintEditorUtils::PropertyValueToString(Property, reinterpret_cast<uint8*>(CDO), Export, CDO);
        for (auto& V : Blueprint->NewVariables) if (V.VarName == Property->GetFName()) V.DefaultValue = Export;
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        return true;
    }

    bool Dispatcher(const FObject& Op)
    {
        if (Blueprint->BlueprintType != BPTYPE_Normal || !NewName(String(Op, TEXT("name")))) return Reject(TEXT("Dispatcher requires a normal Blueprint and a new member name"));
        FName Name(*String(Op, TEXT("name")));
        FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
        if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, Name, Type)) return Reject(TEXT("Could not add dispatcher"));
        UEdGraph* G = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        G->bEditable = false;
        const auto* Schema = GetDefault<UEdGraphSchema_K2>();
        Schema->CreateDefaultNodesForGraph(*G);
        Schema->CreateFunctionGraphTerminators(*G, static_cast<UClass*>(nullptr));
        Schema->AddExtraFunctionFlags(G, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
        Schema->MarkFunctionEntryAsEditable(G, true);
        Blueprint->DelegateSignatureGraphs.Add(G);
        auto* Entry = EntryFor(G);
        if (!Entry || !EventParameters(Entry, Op)) return false;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool EventParameters(UK2Node_EditablePinBase* Event, const FObject& Op)
    {
        const FValues* Params = nullptr;
        if (!Op->HasField(TEXT("parameters"))) return true;
        if (!Op->TryGetArrayField(TEXT("parameters"), Params)) return Reject(TEXT("parameters must be an array"));
        for (const auto& V : *Params)
        {
            if (V->Type != EJson::Object) return Reject(TEXT("Parameter must be an object"));
            auto P = V->AsObject(); FEdGraphPinType Type;
            if (!ReadType(P, Type)) return false;
            FName Name(*String(P, TEXT("name")));
            if (Name.IsNone() || Event->FindPin(Name)) return Reject(TEXT("Invalid or duplicate event parameter"));
            if (P->HasField(TEXT("direction")) && String(P, TEXT("direction")) != TEXT("input")) return Reject(TEXT("Event/dispatcher parameters are input only"));
            if (P->HasField(TEXT("default"))) return Reject(TEXT("Event parameters do not support defaults"));
            Event->CreateUserDefinedPin(Name, Type, EGPD_Output, false);
        }
        return true;
    }

    bool ImplementInterface(const FObject& Op)
    {
        if (Blueprint->BlueprintType != BPTYPE_Normal) return Reject(TEXT("Interfaces can only be implemented on normal Blueprints"));
        UClass* Interface = LoadObject<UClass>(nullptr, *String(Op, TEXT("interface")));
        if (!Interface || !Interface->HasAnyClassFlags(CLASS_Interface)) return Reject(TEXT("Interface class not found"));
        if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, FTopLevelAssetPath(Interface))) return Reject(TEXT("Interface already implemented or incompatible"));
        return true;
    }

    bool InterfaceEvent(const FObject& Op, UEdGraph* G, UEdGraphNode*& Added)
    {
        UClass* Interface = LoadObject<UClass>(nullptr, *String(Op, TEXT("owner")));
        UFunction* Function = Interface ? Interface->FindFunctionByName(FName(*String(Op, TEXT("function")))) : nullptr;
        bool Implemented = false;
        for (const auto& I : Blueprint->ImplementedInterfaces) if (I.Interface == Interface) Implemented = true;
        if (!Interface || !Interface->HasAnyClassFlags(CLASS_Interface) || !Function || !Implemented || !Blueprint->UbergraphPages.Contains(G) || !UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(Function)) return Reject(TEXT("Expected an implemented interface event without outputs in EventGraph"));
        for (UEdGraph* Graph : Blueprint->UbergraphPages) for (UEdGraphNode* N : Graph->Nodes)
            if (auto* E = Cast<UK2Node_Event>(N)) if (E->EventReference.GetMemberName() == Function->GetFName()) return Reject(TEXT("Interface event already exists"));
        Added = Create<UK2Node_Event>(G, [Function, Interface](auto* N) { N->EventReference.SetExternalMember(Function->GetFName(), Interface); N->bOverrideFunction = true; });
        return true;
    }

    bool EditParameter(const FObject& Op)
    {
        UEdGraph* G = Graph(String(Op, TEXT("graph"))); if (!G) return false;
        TArray<UK2Node_EditablePinBase*> Targets;
        const FString Direction = String(Op, TEXT("direction"));
        if (Op->HasField(TEXT("node")))
        {
            auto* Event = Cast<UK2Node_CustomEvent>(ResolveNode(G, String(Op, TEXT("node"))));
            if (!Event || Direction != TEXT("input")) return Reject(TEXT("Custom event input required"));
            Targets.Add(Event);
        }
        else for (UEdGraphNode* N : G->Nodes)
        {
            if ((Direction == TEXT("input") && Cast<UK2Node_FunctionEntry>(N)) || (Direction == TEXT("output") && Cast<UK2Node_FunctionResult>(N))) Targets.Add(CastChecked<UK2Node_EditablePinBase>(N));
        }
        if (Targets.IsEmpty()) return Reject(TEXT("No signature nodes for direction"));
        FName Name(*String(Op, TEXT("name")));
        const FString Action = String(Op, TEXT("action"));
        for (auto* Target : Targets)
        {
            auto* Info = Target->UserDefinedPins.FindByPredicate([Name](const auto& P) { return P->PinName == Name; });
            if (Action == TEXT("add"))
            {
                if (Info || Name.IsNone() || Target->FindPin(Name)) return Reject(TEXT("Invalid or duplicate parameter"));
                FEdGraphPinType Type; if (!ReadType(Op, Type)) return false;
                if ((Blueprint->DelegateSignatureGraphs.Contains(G) || Cast<UK2Node_CustomEvent>(Target)) && Direction != TEXT("input")) return Reject(TEXT("Event/dispatcher input required"));
                UEdGraphPin* Added = Target->CreateUserDefinedPin(Name, Type, Direction == TEXT("input") ? EGPD_Output : EGPD_Input, false);
                if (Direction == TEXT("output"))
                {
                    GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValueBasedOnType(Added);
                    for (auto& NewInfo : Target->UserDefinedPins) if (NewInfo->PinName == Name) NewInfo->PinDefaultValue = Added->DefaultValue;
                }
                continue;
            }
            if (!Info) return Reject(TEXT("Parameter not found"));
            UEdGraphPin* Pin = Target->FindPin(Name);
            if (!Pin) return Reject(TEXT("Signature pin not found"));
            if (Action == TEXT("rename"))
            {
                FName NewName(*String(Op, TEXT("new_name")));
                if (NewName.IsNone() || Target->FindPin(NewName)) return Reject(TEXT("Invalid or duplicate parameter name"));
                (*Info)->PinName = NewName; Pin->PinName = NewName;
            }
            else if (Action == TEXT("remove"))
            {
                if (Pin->LinkedTo.Num() || Pin->SubPins.Num()) return Reject(TEXT("Disconnect/recombine signature pin before removing"));
                Target->RemoveUserDefinedPin(*Info);
            }
            else if (Action == TEXT("type"))
            {
                if (Pin->LinkedTo.Num() || Pin->SubPins.Num()) return Reject(TEXT("Disconnect/recombine signature pin before changing type"));
                FEdGraphPinType Type; if (!ReadType(Op, Type)) return false;
                (*Info)->PinType = Type; (*Info)->PinDefaultValue.Empty(); Pin->PinType = Type;
            }
            else if (Action == TEXT("default"))
            {
                FString Value;
                if (Cast<UK2Node_CustomEvent>(Target) || Blueprint->DelegateSignatureGraphs.Contains(G) || Direction != TEXT("input") || !Op->HasTypedField<EJson::String>(TEXT("value")) || !Op->TryGetStringField(TEXT("value"), Value) || !ValidateValue((*Info)->PinType, Value)) return Reject(TEXT("Invalid input default"));
                if (!Target->ModifyUserDefinedPinDefaultValue(*Info, Value)) return Reject(TEXT("Parameter default rejected by K2 schema"));
            }
            else return Reject(TEXT("Parameter action must be add/rename/remove/type/default"));
            Target->ReconstructNode();
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool Rename(const FObject& Op)
    {
        FName Old(*String(Op, TEXT("name"))), New(*String(Op, TEXT("new_name")));
        if (!NewName(New.ToString())) return false;
        if (String(Op, TEXT("op")) == TEXT("rename_function"))
        {
            UEdGraph* G = Graph(Old.ToString());
            if (!G || (!Blueprint->FunctionGraphs.Contains(G) && !Blueprint->DelegateSignatureGraphs.Contains(G))) return Reject(TEXT("Owned function or dispatcher graph required"));
            FBlueprintEditorUtils::RenameGraph(G, New.ToString());
        }
        else if (Op->HasField(TEXT("graph")))
        {
            UEdGraph* G = Graph(String(Op, TEXT("graph"))); if (!G) return false;
            auto* E = EntryFor(G); if (!E) return false;
            if (!E->LocalVariables.ContainsByPredicate([Old](const auto& V) { return V.VarName == Old; })) return Reject(TEXT("Local not found"));
            if (E->LocalVariables.ContainsByPredicate([New](const auto& V) { return V.VarName == New; })) return Reject(TEXT("Local target name already exists"));
            UFunction* Scope = Blueprint->SkeletonGeneratedClass->FindFunctionByName(G->GetFName());
            if (!Scope) return Reject(TEXT("Local scope not found"));
            FBlueprintEditorUtils::RenameLocalVariable(Blueprint, Scope, Old, New);
        }
        else
        {
            if (!Blueprint->NewVariables.ContainsByPredicate([Old](const auto& V) { return V.VarName == Old; })) return Reject(TEXT("Owned member not found"));
            FBlueprintEditorUtils::RenameMemberVariable(Blueprint, Old, New);
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        return true;
    }

    bool PinEdit(const FObject& Op, UEdGraph* G)
    {
        auto* N = Cast<UK2Node>(ResolveNode(G, String(Op, TEXT("node")))); if (!N) return Reject(TEXT("K2 node required"));
        const FString Action = String(Op, TEXT("action"));
        if (Action == TEXT("add"))
        {
            if (auto* A = Cast<UK2Node_MakeArray>(N)) A->AddInputPin();
            else if (auto* S = Cast<UK2Node_ExecutionSequence>(N)) S->AddInputPin();
            else if (auto* Select = Cast<UK2Node_Select>(N)) { if (!Select->CanAddPin()) return Reject(TEXT("Select cannot add a pin")); Select->AddInputPin(); }
            else return Reject(TEXT("Dynamic pins supported on Make Array, Sequence and Select"));
            return true;
        }
        UEdGraphPin* Pin = N->FindPin(FName(*String(Op, TEXT("pin"))));
        if (!Pin || Pin->bOrphanedPin) return Reject(TEXT("Pin not found"));
        const auto* Schema = GetDefault<UEdGraphSchema_K2>();
        if (Action == TEXT("split"))
        {
            if (Pin->LinkedTo.Num() || !Schema->CanSplitStructPin(*Pin)) return Reject(TEXT("Pin cannot be split (disconnect it first)"));
            Schema->SplitPin(Pin); return !Pin->SubPins.IsEmpty() || Reject(TEXT("Split failed"));
        }
        if (Action == TEXT("recombine"))
        {
            if (Pin->ParentPin) Pin = Pin->ParentPin;
            if (Pin->SubPins.IsEmpty()) return Reject(TEXT("Pin is not split"));
            for (auto* Sub : Pin->SubPins) if (Sub->LinkedTo.Num() || Sub->SubPins.Num()) return Reject(TEXT("Disconnect and recombine child pins first"));
            Schema->RecombinePin(Pin->SubPins[0]); return Pin->SubPins.IsEmpty() || Reject(TEXT("Recombine failed"));
        }
        if (Action != TEXT("remove") || Pin->LinkedTo.Num() || Pin->SubPins.Num()) return Reject(TEXT("Remove requires an unconnected, unsplit dynamic pin"));
        if (auto* A = Cast<UK2Node_MakeArray>(N)) { if (Pin->Direction != EGPD_Input) return Reject(TEXT("Array input required")); A->RemoveInputPin(Pin); }
        else if (auto* S = Cast<UK2Node_ExecutionSequence>(N)) { if (Pin->Direction != EGPD_Output || !S->CanRemoveExecutionPin()) return Reject(TEXT("Sequence output cannot be removed")); S->RemovePinFromExecutionNode(Pin); }
        else if (auto* Select = Cast<UK2Node_Select>(N))
        {
            TArray<UEdGraphPin*> Options; Select->GetOptionPins(Options);
            if (!Select->CanRemoveOptionPinToNode() || Options.Last() != Pin) return Reject(TEXT("Only the last removable Select option may be removed"));
            Select->RemoveOptionPinToNode();
        }
        else return Reject(TEXT("Unsupported dynamic pin node"));
        return true;
    }

    bool ReplaceNode(const FObject& Op, UEdGraph* G)
    {
        UEdGraphNode* Old = ResolveNode(G, String(Op, TEXT("node"))); if (!Old) return false;
        if (Cast<UK2Node_Event>(Old) || Cast<UK2Node_FunctionEntry>(Old) || Cast<UK2Node_FunctionResult>(Old) || Cast<UK2Node_MacroInstance>(Old)) return Reject(TEXT("Cannot replace entry/result/event/macro nodes"));
        const FObject* Replacement = nullptr;
        if (!Op->TryGetObjectField(TEXT("replacement"), Replacement)) return Reject(TEXT("replacement requires add_node fields"));
        auto Spec = MakeShared<FJsonObject>(); Spec->Values = (*Replacement)->Values;
        Spec->SetStringField(TEXT("op"), TEXT("add_node")); Spec->SetStringField(TEXT("graph"), G->GetPathName(Blueprint));
        if (!Apply(Spec)) return false;
        UEdGraphNode* New = Aliases.FindRef(String(Spec, TEXT("id")));
        if (!New || Cast<UK2Node_Event>(New) || Cast<UK2Node_FunctionResult>(New)) return Reject(TEXT("Invalid replacement node"));
        const FObject* Map = nullptr; Op->TryGetObjectField(TEXT("pin_map"), Map);
        TSet<UEdGraphPin*> Used;
        TArray<TPair<UEdGraphPin*, UEdGraphPin*>> Pairs;
        for (UEdGraphPin* P : Old->Pins)
        {
            if (P->ParentPin || P->SubPins.Num()) return Reject(TEXT("Recombine pins before replacing a node"));
            FString Name = P->PinName.ToString(); if (Map) (*Map)->TryGetStringField(Name, Name);
            UEdGraphPin* Q = New->FindPin(FName(*Name), P->Direction);
            if (!Q || Q->PinType != P->PinType || Used.Contains(Q)) return Reject(TEXT("Every old pin must map to a unique replacement pin of the same type"));
            Used.Add(Q); Pairs.Emplace(P,Q);
        }
        for (const auto& Pair : Pairs) Pair.Value->MovePersistentDataFromOldPin(*Pair.Key);
        G->RemoveNode(Old);
        for (auto& Source : SourceNodes) if (Source.Value == Old) Source.Value = New;
        SourceNodes.Add(G->GetPathName(Blueprint) + TEXT("|") + Old->NodeGuid.ToString(), New);
        for (auto It = Aliases.CreateIterator(); It; ++It) if (It.Value() == Old) It.RemoveCurrent();
        return true;
    }
