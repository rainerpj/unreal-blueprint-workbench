#requires -Version 7.0
param([Parameter(Mandatory)][string]$ProjectFile, [Parameter(Mandatory)][string]$EngineRoot)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectFile).Path
if ([IO.Path]::GetExtension($project) -ne '.uproject') { throw 'ProjectFile must be a .uproject.' }
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { throw 'Save and close UE editors before installation.' }
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$projectRoot = Split-Path -Parent $project
$root = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $projectRoot 'Plugins/BlueprintWorkbenchAssist'
if (Test-Path -LiteralPath $destination) { throw 'Bridge already exists; back up and review the upgrade explicitly.' }
$manifest = Get-Content -LiteralPath "$root/AssistBuild.json" -Raw | ConvertFrom-Json
foreach ($required in @('BlueprintWorkbenchAssist.uplugin','Binaries/Win64/UnrealEditor-BlueprintWorkbenchAssist.dll','Scripts/Invoke-Assist.ps1','Scripts/RunAssist.py','Scripts/RunBatchAssist.py','Scripts/FinalizeAssist.py','Scripts/FinalizeBatchAssist.py','Scripts/AssistSettings.py','Scripts/AssistVerification.py','Scripts/BatchVerification.py','Scripts/BatchPlanning.py')) {
    if (-not (Test-Path -LiteralPath "$root/$required" -PathType Leaf)) { throw "Incomplete bridge package: $required" }
}
$engineModules = Get-Content -LiteralPath "$engine/Engine/Binaries/Win64/UnrealEditor.modules" -Raw | ConvertFrom-Json
$bridgeModules = Get-Content -LiteralPath "$root/Binaries/Win64/UnrealEditor.modules" -Raw | ConvertFrom-Json
if (-not $manifest.build_id -or $manifest.build_id -ne $engineModules.BuildId -or $manifest.build_id -ne $bridgeModules.BuildId) { throw 'Bridge engine BuildId mismatch.' }
function Find-Dependency([string]$Name) {
    $local = @(Get-ChildItem -LiteralPath "$projectRoot/Plugins" -Filter "$Name.uplugin" -Recurse -File -ErrorAction SilentlyContinue)
    if ($local.Count -eq 1) { return $local[0] }
    if ($local.Count -gt 1) { throw "Ambiguous project dependency: $Name" }
    $global = @(Get-ChildItem -LiteralPath "$engine/Engine/Plugins" -Filter "$Name.uplugin" -Recurse -File)
    if ($global.Count -ne 1) { throw "Install the matching $Name separately before installing the bridge." }
    return $global[0]
}
foreach ($name in @('BlueprintAssist','BlueprintWorkbench')) {
    $file = Find-Dependency $name
    $dependency = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    if ($name -eq 'BlueprintAssist' -and $dependency.VersionName -ne $manifest.blueprint_assist_version) { throw "Blueprint Assist version mismatch; requires $($manifest.blueprint_assist_version)." }
    if ($name -eq 'BlueprintWorkbench' -and [version]$dependency.VersionName -lt [version]$manifest.core_minimum_version) { throw 'Install BlueprintWorkbench 0.5.0 or newer first.' }
    $dependencyModules = Get-Content -LiteralPath (Join-Path $file.DirectoryName 'Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
    if ($dependencyModules.BuildId -ne $manifest.build_id -or -not (Test-Path -LiteralPath (Join-Path $file.DirectoryName "Binaries/Win64/UnrealEditor-$name.dll"))) { throw "Dependency binary mismatch: $name" }
}
$config = Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
$plugins = @($config.Plugins | Where-Object { $_ })
foreach ($name in @('BlueprintWorkbench','BlueprintAssist','BlueprintWorkbenchAssist')) {
    $existing = @($plugins | Where-Object { $_.Name -eq $name })
    if ($existing.Count -gt 1) { throw "Duplicate plugin reference: $name" }
    if ($existing.Count -eq 1) { $existing[0] | Add-Member -NotePropertyName Enabled -NotePropertyValue $true -Force }
    else { $plugins += [pscustomobject]@{Name=$name;Enabled=$true} }
}
$config | Add-Member -NotePropertyName Plugins -NotePropertyValue $plugins -Force
$backup = "$project.workbench-assist-$([Guid]::NewGuid().ToString('N')).bak"
Copy-Item -LiteralPath $project -Destination $backup
New-Item -ItemType Directory -Path $destination -Force | Out-Null
foreach ($entry in @('BlueprintWorkbenchAssist.uplugin','Source','Scripts','Config','README.md','AGENTS.md','LICENSE','Binaries','Intermediate','AssistBuild.json')) {
    if (Test-Path -LiteralPath "$root/$entry") { Copy-Item -LiteralPath "$root/$entry" -Destination $destination -Recurse }
}
$config | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $project -Encoding UTF8
Write-Output "Installed and enabled bridge: $destination. Project descriptor backup: $backup"
