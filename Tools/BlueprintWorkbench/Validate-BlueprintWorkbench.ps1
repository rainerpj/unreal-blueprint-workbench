param([Parameter(Mandatory)][string]$ProjectFile, [Parameter(Mandatory)][string]$EngineRoot, [switch]$MemoryCache)
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/../../Plugins/BlueprintWorkbench/Scripts/Validate-BlueprintWorkbench.ps1" @PSBoundParameters
