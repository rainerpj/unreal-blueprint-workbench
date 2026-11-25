#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('AssistDistribution-'+[Guid]::NewGuid().ToString('N'))
$package = "$root/package"
Copy-Item -LiteralPath (Split-Path -Parent $PSScriptRoot) -Destination $package -Recurse
function Write-Json($Path, $Value) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    $Value | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $Path -Encoding UTF8
}
Write-Json "$package/AssistBuild.json" @{build_id='test';blueprint_assist_version='4.2.22';core_minimum_version='0.5.0'}
Write-Json "$package/Binaries/Win64/UnrealEditor.modules" @{BuildId='test'}
Set-Content "$package/Binaries/Win64/UnrealEditor-BlueprintWorkbenchAssist.dll" 'synthetic-test-only'
$engine = "$root/engine"
Write-Json "$engine/Engine/Binaries/Win64/UnrealEditor.modules" @{BuildId='wrong'}
$project = "$root/project/Host.uproject"
Write-Json $project @{FileVersion=3;Description='preserve';Plugins=@(@{Name='BlueprintWorkbench';Enabled=$false;TargetAllowList=@('Editor')})}
function Expect-Rejection($Pattern) {
    $failed = $false
    try { & "$package/Scripts/Install-Assist.ps1" -ProjectFile $project -EngineRoot $engine }
    catch { if ($_.Exception.Message -notlike $Pattern) { throw }; $failed=$true }
    if (-not $failed) { throw 'Expected rejection.' }
    if (Test-Path "$root/project/Plugins/BlueprintWorkbenchAssist") { throw 'Rejected install created a destination.' }
}
Expect-Rejection '*BuildId mismatch*'
Write-Json "$engine/Engine/Binaries/Win64/UnrealEditor.modules" @{BuildId='test'}
New-Item -ItemType Directory -Force -Path "$engine/Engine/Plugins" | Out-Null
Expect-Rejection '*Install the matching BlueprintAssist*'
foreach ($name in @('BlueprintAssist','BlueprintWorkbench')) {
    $dep = "$engine/Engine/Plugins/$name"
    $version = if ($name -eq 'BlueprintAssist') {'wrong'} else {'0.5.0'}
    Write-Json "$dep/$name.uplugin" @{VersionName=$version}
    Write-Json "$dep/Binaries/Win64/UnrealEditor.modules" @{BuildId='test'}
    Set-Content "$dep/Binaries/Win64/UnrealEditor-$name.dll" 'synthetic-test-only'
}
Expect-Rejection '*Blueprint Assist version mismatch*'
Write-Json "$engine/Engine/Plugins/BlueprintAssist/BlueprintAssist.uplugin" @{VersionName='4.2.22'}
& "$package/Scripts/Install-Assist.ps1" -ProjectFile $project -EngineRoot $engine
$config = Get-Content $project -Raw | ConvertFrom-Json
if ($config.Description -ne 'preserve' -or ($config.Plugins | Where-Object Name -eq BlueprintWorkbench).TargetAllowList[0] -ne 'Editor') { throw 'Existing project fields lost.' }
if (@($config.Plugins | Where-Object Enabled).Count -ne 3) { throw 'Dependencies not enabled.' }
if (@(Get-ChildItem "$project.workbench-assist-*.bak").Count -ne 1) { throw 'Missing project descriptor backup.' }
$failed=$false
try { & "$package/Scripts/Install-Assist.ps1" -ProjectFile $project -EngineRoot $engine }
catch { if ($_.Exception.Message -notlike 'Bridge already exists*') { throw }; $failed=$true }
if (-not $failed) { throw 'Existing install was overwritten.' }
Write-Output '5 installer contracts passed: BuildId, missing dependency, BA version, install/enable/preserve/backup, overwrite refusal.'
