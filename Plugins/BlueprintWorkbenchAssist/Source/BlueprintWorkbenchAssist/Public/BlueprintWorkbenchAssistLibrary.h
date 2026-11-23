#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintWorkbenchAssistLibrary.generated.h"

/** UI editor bridge. Operations return JSON; no function saves an asset. */
UCLASS()
class BLUEPRINTWORKBENCHASSIST_API UBlueprintWorkbenchAssistLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench|Assist")
    static FString OpenGraph(const FString& AssetPath, const FString& GraphPath);
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench|Assist")
    static FString GraphState(const FString& AssetPath, const FString& GraphPath, bool RefreshSizes = false);
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench|Assist")
    static FString FormatFunctionGraph(const FString& AssetPath, const FString& GraphPath);
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench|Assist")
    static FString CompileChecked(const FString& AssetPath);
    UFUNCTION(BlueprintCallable, Category="Blueprint Workbench|Assist")
    static bool CaptureGraph(const FString& AssetPath, const FString& GraphPath, const FString& Filename, bool ZoomToFit = false);
};
