using UnrealBuildTool;

public class BlueprintWorkbenchAssist : ModuleRules
{
    public BlueprintWorkbenchAssist(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "Kismet", "KismetCompiler", "BlueprintGraph", "GraphEditor",
            "Slate", "SlateCore", "Json", "ImageWrapper", "BlueprintAssist"
        });
    }
}
