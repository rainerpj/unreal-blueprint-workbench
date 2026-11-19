param([Parameter(Mandatory)][string]$HostDirectory, [Parameter(Mandatory)][string]$EngineRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$target = [IO.Path]::GetFullPath($HostDirectory)
if (Test-Path -LiteralPath $target) { throw "Host directory must not exist: $target" }
$engineVersion = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if (-not (Test-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'))) { throw 'Engine executable missing.' }
New-Item -ItemType Directory -Path "$target/Source/WorkbenchHost" -Force | Out-Null
$project = @{DisableEnginePluginsByDefault=$true;FileVersion=3;EngineAssociation="$($engineVersion.MajorVersion).$($engineVersion.MinorVersion)";Modules=@(@{Name='WorkbenchHost';Type='Runtime';LoadingPhase='Default'});Plugins=@(@{Name='BlueprintWorkbench';Enabled=$true})}
$project | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath "$target/WorkbenchHost.uproject" -Encoding UTF8
@'
using UnrealBuildTool;
public class WorkbenchHost : ModuleRules {
    public WorkbenchHost(ReadOnlyTargetRules Target) : base(Target) {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
    }
}
'@ | Set-Content -LiteralPath "$target/Source/WorkbenchHost/WorkbenchHost.Build.cs" -Encoding UTF8
@'
#include "Modules/ModuleManager.h"
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, WorkbenchHost, "WorkbenchHost");
'@ | Set-Content -LiteralPath "$target/Source/WorkbenchHost/WorkbenchHost.cpp" -Encoding UTF8
@'
using UnrealBuildTool;
public class WorkbenchHostEditorTarget : TargetRules {
    public WorkbenchHostEditorTarget(TargetInfo Target) : base(Target) {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("WorkbenchHost");
    }
}
'@ | Set-Content -LiteralPath "$target/Source/WorkbenchHostEditor.Target.cs" -Encoding UTF8
$plugin = Join-Path $target 'Plugins/BlueprintWorkbench'
New-Item -ItemType Directory -Path $plugin -Force | Out-Null
$sourceRoot = Split-Path -Parent $PSScriptRoot
Copy-WorkbenchSource $sourceRoot $plugin
Write-Output "$target/WorkbenchHost.uproject"
