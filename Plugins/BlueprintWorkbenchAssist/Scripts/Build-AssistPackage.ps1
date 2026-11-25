#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$root = Split-Path -Parent $PSScriptRoot
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new output directory.' }
$baFiles = @(Get-ChildItem -LiteralPath "$engine/Engine/Plugins" -Filter BlueprintAssist.uplugin -Recurse -File)
$coreFiles = @(Get-ChildItem -LiteralPath "$engine/Engine/Plugins" -Filter BlueprintWorkbench.uplugin -Recurse -File)
if ($baFiles.Count -ne 1 -or $coreFiles.Count -ne 1) { throw 'Install exactly one engine BlueprintAssist and BlueprintWorkbench before UAT packaging.' }
$ba = Get-Content -LiteralPath $baFiles[0].FullName -Raw | ConvertFrom-Json
$version = Get-Content -LiteralPath "$engine/Engine/Build/Build.version" -Raw | ConvertFrom-Json
$descriptor = Get-Content -LiteralPath "$root/BlueprintWorkbenchAssist.uplugin" -Raw | ConvertFrom-Json
New-Item -ItemType Directory -Path $output | Out-Null
$package = Join-Path $output 'BlueprintWorkbenchAssist'
& "$engine/Engine/Build/BatchFiles/RunUAT.bat" BuildPlugin "-Plugin=$root/BlueprintWorkbenchAssist.uplugin" "-Package=$package" -TargetPlatforms=Win64 -HostPlatforms=Win64 -NoP4 *> "$output/build.log"
if ($LASTEXITCODE -ne 0) { throw "UAT failed; see $output/build.log" }
foreach ($file in @('Binaries/Win64/UnrealEditor-BlueprintWorkbenchAssist.dll','Binaries/Win64/UnrealEditor.modules','Scripts/Invoke-Assist.ps1','Scripts/Install-Assist.ps1','Scripts/RunBatchAssist.py','LICENSE','Config/FilterPlugin.ini')) {
    if (-not (Test-Path -LiteralPath "$package/$file")) { throw "Missing package file: $file" }
}
$modules = Get-Content -LiteralPath "$package/Binaries/Win64/UnrealEditor.modules" -Raw | ConvertFrom-Json
@{engine_version="$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)";build_id=$modules.BuildId;platform='Win64';blueprint_assist_version=$ba.VersionName;core_minimum_version='0.5.0'} | ConvertTo-Json | Set-Content -LiteralPath "$package/AssistBuild.json" -Encoding UTF8
$zip = "$output/BlueprintWorkbenchAssist-$($descriptor.VersionName)-UE$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)-Win64.zip"
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in (Get-ChildItem -LiteralPath $package -Recurse -File)) {
        if ($file.Extension -eq '.pdb') { continue }
        $relative = [IO.Path]::GetRelativePath($output,$file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative) | Out-Null
    }
} finally { $archive.Dispose() }
Write-Output "Built $zip; validate in a fresh Blueprint-only host before publishing."
