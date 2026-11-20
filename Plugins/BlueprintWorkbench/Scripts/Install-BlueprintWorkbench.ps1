param([Parameter(Mandatory)][string]$ProjectFile, [string]$EngineRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$project = (Resolve-Path -LiteralPath $ProjectFile).Path
if ([IO.Path]::GetExtension($project) -ne '.uproject') { throw 'ProjectFile must be a .uproject file.' }
$destination = Join-Path (Split-Path -Parent $project) 'Plugins/BlueprintWorkbench'
if (Test-Path -LiteralPath $destination) { throw "Plugin already exists; review and migrate it explicitly: $destination" }
$sourceRoot = Split-Path -Parent $PSScriptRoot
$binaryManifest = Join-Path $sourceRoot 'WorkbenchBuild.json'
if (Test-Path -LiteralPath $binaryManifest) {
    if (-not $EngineRoot) { throw 'A precompiled package requires EngineRoot to verify the engine build.' }
    $build = Get-Content -LiteralPath $binaryManifest -Raw | ConvertFrom-Json
    $engineModules = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
    $pluginModules = Get-Content -LiteralPath (Join-Path $sourceRoot 'Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
    if (-not $build.build_id -or $build.build_id -ne $engineModules.BuildId -or $build.build_id -ne $pluginModules.BuildId) { throw 'Precompiled plugin engine build mismatch. Use the matching release or compile from source.' }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot 'Binaries/Win64/UnrealEditor-BlueprintWorkbench.dll'))) { throw 'Precompiled plugin DLL missing.' }
}
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-WorkbenchSource $sourceRoot $destination
if (Test-Path -LiteralPath $binaryManifest) {
    foreach ($entry in @('Binaries','Intermediate','WorkbenchBuild.json')) {
        $path = Join-Path $sourceRoot $entry
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $destination -Recurse }
    }
    Write-Output "Precompiled plugin installed: $destination (UE $($build.engine_version))."
    return
}
if (-not (Test-Path -LiteralPath (Join-Path $destination 'BlueprintWorkbench.uplugin'))) { throw 'Installation descriptor missing.' }
Write-Output "Source installed: $destination. Build against the target engine before opening the project."
