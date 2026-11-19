param(
    [Parameter(Mandatory)][string]$ProjectFile,
    [Parameter(Mandatory)][string]$AssetPath,
    [string]$PatchFile,
    [string]$PresentationFile,
    [string]$DestinationPackage,
    [switch]$WriteBack,
    [switch]$TemporaryCopy,
    [string]$ReportFile,
    [Parameter(Mandatory)][string]$EngineRoot
)
$ErrorActionPreference = 'Stop'
& "$PSScriptRoot/../../Plugins/BlueprintWorkbench/Scripts/Invoke-BlueprintWorkbench.ps1" @PSBoundParameters
