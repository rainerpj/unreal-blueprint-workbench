# Filesystem-only installation tests. Synthetic binary files do not establish UE loading compatibility.
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ('WorkbenchDistributionTest-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
function New-TestProject([string]$Name) {
    $directory = Join-Path $root $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $project = Join-Path $directory 'Test.uproject'
    '{"FileVersion":3}' | Set-Content -LiteralPath $project -Encoding UTF8
    return $project
}
$project = New-TestProject 'Source'
& "$PSScriptRoot/Install-BlueprintWorkbench.ps1" -ProjectFile $project
$installed = Join-Path (Split-Path -Parent $project) 'Plugins/BlueprintWorkbench'
foreach ($file in @('BlueprintWorkbench.uplugin','Config/FilterPlugin.ini','Scripts/RunWorkbench.py')) {
    if (-not (Test-Path -LiteralPath (Join-Path $installed $file))) { throw "Source install missing $file" }
}
$rejected = $false
try { & "$PSScriptRoot/Install-BlueprintWorkbench.ps1" -ProjectFile $project } catch {
    if ($_.Exception.Message -notlike 'Plugin already exists*') { throw }
    $rejected = $true
}
if (-not $rejected) { throw 'Existing plugin overwrite was not rejected.' }
# Turn the installed source into a synthetic binary release to exercise install selection and BuildId checks.
New-Item -ItemType Directory -Path "$installed/Binaries/Win64", "$installed/Intermediate" | Out-Null
'synthetic test DLL' | Set-Content -LiteralPath "$installed/Binaries/Win64/UnrealEditor-BlueprintWorkbench.dll"
'{"BuildId":"test-build"}' | Set-Content -LiteralPath "$installed/Binaries/Win64/UnrealEditor.modules"
'{"build_id":"test-build","engine_version":"test"}' | Set-Content -LiteralPath "$installed/WorkbenchBuild.json"
'precompile fixture' | Set-Content -LiteralPath "$installed/Intermediate/fixture.precompiled"
$engine = Join-Path $root 'EngineFixture'
New-Item -ItemType Directory -Path "$engine/Engine/Binaries/Win64" -Force | Out-Null
'{"BuildId":"test-build"}' | Set-Content -LiteralPath "$engine/Engine/Binaries/Win64/UnrealEditor.modules"
$binaryProject = New-TestProject 'Binary'
& "$installed/Scripts/Install-BlueprintWorkbench.ps1" -ProjectFile $binaryProject -EngineRoot $engine
foreach ($file in @('Binaries/Win64/UnrealEditor-BlueprintWorkbench.dll','Intermediate/fixture.precompiled','WorkbenchBuild.json','Config/FilterPlugin.ini')) {
    if (-not (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $binaryProject) "Plugins/BlueprintWorkbench/$file"))) { throw "Binary install missing $file" }
}
$wrongProject = New-TestProject 'WrongEngine'
'{"BuildId":"different-build"}' | Set-Content -LiteralPath "$engine/Engine/Binaries/Win64/UnrealEditor.modules"
$rejected = $false
try { & "$installed/Scripts/Install-BlueprintWorkbench.ps1" -ProjectFile $wrongProject -EngineRoot $engine } catch {
    if ($_.Exception.Message -notlike 'Precompiled plugin engine build mismatch*') { throw }
    $rejected = $true
}
if (-not $rejected -or (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $wrongProject) 'Plugins/BlueprintWorkbench'))) { throw 'Build mismatch must fail before installing files.' }
$archivePath = Join-Path $root 'source.zip'
& "$PSScriptRoot/Package-BlueprintWorkbench.ps1" -OutputFile $archivePath
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $names = @($archive.Entries.FullName)
    if ($names -notcontains 'BlueprintWorkbench/Config/FilterPlugin.ini' -or $names -notcontains 'BlueprintWorkbench/Scripts/RunWorkbench.py') { throw 'Source archive is missing required files.' }
    if ($names -match '/Binaries/|/__pycache__/|\.pyc$') { throw 'Source archive contains build/cache artifacts.' }
} finally { $archive.Dispose() }
Write-Output "Distribution checks passed: source, overwrite refusal, binary payload, BuildId rejection, source ZIP. Fixtures: $root"
