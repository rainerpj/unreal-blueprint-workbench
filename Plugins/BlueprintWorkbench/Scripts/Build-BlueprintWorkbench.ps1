param([Parameter(Mandatory)][string]$ProjectFile, [Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$EditorTarget)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$context = Get-WorkbenchContext $ProjectFile $EngineRoot
$build = Join-Path $context.Engine 'Engine/Build/BatchFiles/Build.bat'
& $build $EditorTarget Win64 Development "-Project=$($context.Project)" -WaitMutex -NoHotReloadFromIDE -NoUBA
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
