param([Parameter(Mandatory)][string]$ProjectFile)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$project = (Resolve-Path -LiteralPath $ProjectFile).Path
if ([IO.Path]::GetExtension($project) -ne '.uproject') { throw 'ProjectFile must be a .uproject file.' }
$destination = Join-Path (Split-Path -Parent $project) 'Plugins/BlueprintWorkbench'
if (Test-Path -LiteralPath $destination) { throw "Plugin already exists; review and migrate it explicitly: $destination" }
$sourceRoot = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-WorkbenchSource $sourceRoot $destination
if (-not (Test-Path -LiteralPath (Join-Path $destination 'BlueprintWorkbench.uplugin'))) { throw 'Installation descriptor missing.' }
Write-Output "Source installed: $destination. Build against the target engine before opening the project."
