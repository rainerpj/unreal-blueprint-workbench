#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintWorkbenchLibrary.generated.h"

/** Editor-only tools. Copy editing by default; explicit commandlet writeback keeps a disk backup. */
UCLASS()
class BLUEPRINTWORKBENCH_API UBlueprintWorkbenchLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Isolated commandlet only: preserve original class identity in memory. Preview never saves; caller must exit the process before writeback. */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString ApplyPatchInIsolatedCommandlet(const FString& AssetPath, const FString& PatchJson, bool bWriteBack = false);

    /** Create a new enum or struct from an explicit schema; empty destination previews only. */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString CreateSchemaAsset(const FString& Kind, const FString& SchemaJson, const FString& DestinationPackage = TEXT(""));

    /** Create a normal Blueprint, function library or interface; empty destination previews, existing packages are refused. */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString CreateBlueprint(const FString& Kind, const FString& ParentClass, const FString& PatchJson, const FString& DestinationPackage = TEXT(""));

    /** Read-only saved-package referencer and direct K2 call/variable impact report. Does not modify callers. */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString FindMemberReferences(const FString& AssetPath, const FString& MemberName);

    /** Export graph/node/pin identities, links and member variables as JSON. Does not compile or save. */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString InspectBlueprint(const FString& AssetPath);

    /** Apply an ordered JSON patch to a duplicate and compile it. Empty DestinationPackage previews
     * in memory. A new /Game package saves a copy only after successful compilation; overwrite is refused.
     * Returns JSON with success, compiled, saved, errors and the resulting graph description.
     */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString ApplyPatch(const FString& AssetPath, const FString& PatchJson, const FString& DestinationPackage = TEXT(""));

    /** Commandlet-only writeback of an existing /Game Blueprint. Preflights a copy, backs up
     * the original .uasset, then edits, compiles and saves the source. Close other editors first.
     * On an operation/compile failure the source is not saved; exit this commandlet before retrying.
     */
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench")
    static FString ApplyPatchToSavedAsset(const FString& AssetPath, const FString& PatchJson);
};
