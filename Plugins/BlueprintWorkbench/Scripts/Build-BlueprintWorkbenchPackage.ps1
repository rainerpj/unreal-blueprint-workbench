#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$uat = Join-Path $engine 'Engine/Build/BatchFiles/RunUAT.bat'
if (-not (Test-Path -LiteralPath $uat -PathType Leaf)) { throw 'RunUAT.bat missing.' }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Output directory must not exist: $output" }
$version = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
$plugin = Join-Path (Split-Path -Parent $PSScriptRoot) 'BlueprintWorkbench.uplugin'
$pluginVersion = (Get-Content -LiteralPath $plugin -Raw | ConvertFrom-Json).VersionName
$package = Join-Path $output 'BlueprintWorkbench'
New-Item -ItemType Directory -Path $output | Out-Null
& $uat BuildPlugin "-Plugin=$plugin" "-Package=$package" -TargetPlatforms=Win64 -HostPlatforms=Win64 -NoP4 2>&1 | Tee-Object -FilePath (Join-Path $output 'build.log')
if ($LASTEXITCODE -ne 0) { throw "UAT failed ($LASTEXITCODE); see $output/build.log" }
foreach ($required in @('BlueprintWorkbench.uplugin','Binaries/Win64/UnrealEditor-BlueprintWorkbench.dll','Binaries/Win64/UnrealEditor.modules','Config/FilterPlugin.ini','Scripts/Invoke-BlueprintWorkbench.ps1','Scripts/RunWorkbench.py','Scripts/Install-BlueprintWorkbench.ps1','Docs/PATCH_FORMAT.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)) { throw "Incomplete package: $required" }
}
$modules = Get-Content -LiteralPath (Join-Path $package 'Binaries/Win64/UnrealEditor.modules') -Raw | ConvertFrom-Json
@{engine_version="$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)";build_id=$modules.BuildId;platform='Win64'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'WorkbenchBuild.json') -Encoding UTF8
$archivePath = Join-Path $output "BlueprintWorkbench-$pluginVersion-UE$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)-Win64.zip"
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in (Get-ChildItem -LiteralPath $package -File -Recurse)) {
        if ($file.Extension -eq '.pdb') { continue }
        $relative = [IO.Path]::GetRelativePath($output, $file.FullName).Replace('\','/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative) | Out-Null
    }
} finally { $archive.Dispose() }
Write-Output "Built Win64 plugin ZIP: $archivePath. Validate in a fresh Blueprint-only project before release."
