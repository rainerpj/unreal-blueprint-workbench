// New schema assets only. Existing content uses explicit graph patch/writeback APIs.
FString UBlueprintWorkbenchLibrary::CreateSchemaAsset(const FString& Kind, const FString& SchemaJson, const FString& DestinationPackage)
{
    using namespace BlueprintWorkbench;
    FObject Out=Result(), Spec;
    if (!IsInGameThread()) return Fail(Out,TEXT("Editor game thread required"));
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SchemaJson),Spec) || !Spec.IsValid()) return Fail(Out,TEXT("Schema object required"));
    const bool Save=!DestinationPackage.IsEmpty();
    if (Save && (!DestinationPackage.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(DestinationPackage) || FindPackage(nullptr,*DestinationPackage) || FPackageName::DoesPackageExist(DestinationPackage))) return Fail(Out,TEXT("New /Game package required"));
    const FValues* Entries=nullptr;
    const FString Field=Kind==TEXT("enum")?TEXT("values"):TEXT("fields");
    if ((Kind!=TEXT("enum") && Kind!=TEXT("struct")) || Spec->Values.Num()!=1 || !Spec->TryGetArrayField(Field,Entries) || Entries->IsEmpty()) return Fail(Out,TEXT("Nonempty enum values or struct fields required"));
    TSet<FString> Names;
    TArray<FEdGraphPinType> Types;
    FPatch Context{nullptr,{},{},{}};
    for (const auto& V:*Entries)
    {
        FString Name;
        if (Kind==TEXT("enum")) { if (!V->TryGetString(Name)) return Fail(Out,TEXT("Enum value must be string")); }
        else
        {
            if (V->Type!=EJson::Object) return Fail(Out,TEXT("Struct field object required"));
            Name=String(V->AsObject(),TEXT("name")); FEdGraphPinType T;
            if (!Context.ReadType(V->AsObject(),T)) return Fail(Out,Context.Error);
            Types.Add(T);
        }
        if (Name.IsEmpty() || Names.Contains(Name)) return Fail(Out,TEXT("Empty or duplicate name"));
        Names.Add(Name);
    }
    // Build in transient package first, including all field defaults, before naming a destination.
    const FString Temp=TEXT("/Temp/WorkbenchSchema_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UPackage* Package=CreatePackage(*Temp);Package->SetFlags(RF_Transient);
    TStrongObjectPtr<UPackage> Keep(Package);
    UObject* Asset=nullptr;
    if (Kind==TEXT("enum"))
    {
        auto* Enum=Cast<UUserDefinedEnum>(FEnumEditorUtils::CreateUserDefinedEnum(Package,*FPackageName::GetLongPackageAssetName(Temp),RF_Public|RF_Standalone));
        Asset=Enum;
        for (int32 I=0;I<Entries->Num();++I)
        {
            FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
            if (!FEnumEditorUtils::SetEnumeratorDisplayName(Enum,I,FText::FromString((*Entries)[I]->AsString()))) return Fail(Out,TEXT("Enum display name rejected"));
        }
    }
    else
    {
        auto* Struct=FStructureEditorUtils::CreateUserDefinedStruct(Package,*FPackageName::GetLongPackageAssetName(Temp),RF_Public|RF_Standalone);Asset=Struct;
        TArray<FGuid> Initial;
        for (const auto& D:FStructureEditorUtils::GetVarDesc(Struct)) Initial.Add(D.VarGuid);
        for (int32 I=0;I<Entries->Num();++I)
        {
            const FObject F=(*Entries)[I]->AsObject();
            if (!FStructureEditorUtils::AddVariable(Struct,Types[I])) return Fail(Out,TEXT("Struct field rejected"));
            const FGuid Guid=FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
            if (!FStructureEditorUtils::RenameVariable(Struct,Guid,String(F,TEXT("name")))) return Fail(Out,TEXT("Struct field name rejected"));
            if (F->HasField(TEXT("default")) && !FStructureEditorUtils::ChangeVariableDefaultValue(Struct,Guid,String(F,TEXT("default")))) return Fail(Out,TEXT("Struct default rejected"));
            if (F->HasField(TEXT("tooltip"))) FStructureEditorUtils::ChangeVariableTooltip(Struct,Guid,String(F,TEXT("tooltip")));
        }
        for (const auto& Guid:Initial) FStructureEditorUtils::RemoveVariable(Struct,Guid);
        FStructureEditorUtils::CompileStructure(Struct);
        if (Struct->Status!=UDSS_UpToDate) return Fail(Out,TEXT("Struct compilation failed"));
    }
    FValues Description;
    if (auto* Enum=Cast<UEnum>(Asset)) for (int32 I=0;I<Enum->NumEnums()-1;++I)
    {
        FObject E=MakeShared<FJsonObject>();E->SetStringField(TEXT("name"),Enum->GetNameStringByIndex(I));E->SetStringField(TEXT("display"),Enum->GetDisplayNameTextByIndex(I).ToString());Description.Add(MakeShared<FJsonValueObject>(E));
    }
    if (auto* Struct=Cast<UUserDefinedStruct>(Asset)) for (const auto& D:FStructureEditorUtils::GetVarDesc(Struct))
    {
        FObject E=MakeShared<FJsonObject>();E->SetStringField(TEXT("name"),D.VarName.ToString());E->SetStringField(TEXT("display"),D.FriendlyName);Description.Add(MakeShared<FJsonValueObject>(E));
    }
    Out->SetArrayField(TEXT("members"),Description);
    if (Save)
    {
        UPackage* Dest=CreatePackage(*DestinationPackage);
        Asset->Rename(*FPackageName::GetLongPackageAssetName(DestinationPackage),Dest,REN_DontCreateRedirectors|REN_NonTransactional);
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        const FString File=FPackageName::LongPackageNameToFilename(DestinationPackage,FPackageName::GetAssetPackageExtension());
        if (!UPackage::SavePackage(Dest,Asset,*File,Args)) return Fail(Out,TEXT("Schema save failed"));
        FAssetRegistryModule::AssetCreated(Asset);Out->SetBoolField(TEXT("saved"),true);
    }
    else {Asset->ClearFlags(RF_Public|RF_Standalone);Package->SetDirtyFlag(false);}
    Out->SetBoolField(TEXT("success"),true);return Json(Out);
}
