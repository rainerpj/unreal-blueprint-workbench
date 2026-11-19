using UnrealBuildTool;

public class BlueprintWorkbench : ModuleRules
{
    public BlueprintWorkbench(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "UnrealEd", "BlueprintGraph", "KismetCompiler", "Json", "AssetRegistry", "UMG", "UMGEditor"
        });
    }
}
