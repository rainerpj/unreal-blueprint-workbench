function Get-WorkbenchContext {
    param([string]$ProjectFile, [string]$EngineRoot)
    $project = (Resolve-Path -LiteralPath $ProjectFile -ErrorAction Stop).Path
    if ([IO.Path]::GetExtension($project) -ne '.uproject') { throw 'ProjectFile must be an existing .uproject file.' }
    $engine = (Resolve-Path -LiteralPath $EngineRoot -ErrorAction Stop).Path
    $editor = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    $versionFile = Join-Path $engine 'Engine/Build/Build.version'
    if (-not (Test-Path -LiteralPath $editor -PathType Leaf) -or -not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { throw "Incomplete engine installation: $engine" }
    $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
    $reports = Join-Path (Split-Path -Parent $project) 'Saved/BlueprintWorkbench'
    New-Item -ItemType Directory -Force -Path $reports | Out-Null
    return @{ Project=$project; Root=(Split-Path -Parent $project); Engine=$engine; Editor=$editor; Reports=$reports; Version="$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)" }
}
function Invoke-WorkbenchPython {
    param($Context, [string]$Script, [string]$LogFile)
    & $Context.Editor $Context.Project -run=pythonscript "-script=$Script" -unattended -nop4 -NullRHI -nosound "-abslog=$LogFile"
    if ($LASTEXITCODE -ne 0) { throw "UE Python failed ($LASTEXITCODE). Log: $LogFile" }
}
function Copy-WorkbenchSource {
    param([string]$SourceRoot, [string]$Destination)
    foreach ($entry in @('BlueprintWorkbench.uplugin','Source','Scripts','Docs','README.md','AGENTS.md')) {
        $path = Join-Path $SourceRoot $entry
        $files = if (Test-Path -LiteralPath $path -PathType Container) { Get-ChildItem -LiteralPath $path -File -Recurse } else { Get-Item -LiteralPath $path -ErrorAction Stop }
        foreach ($file in $files) {
            if ($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -eq '.pyc') { continue }
            $relative = [IO.Path]::GetRelativePath($SourceRoot, $file.FullName)
            $targetFile = Join-Path $Destination $relative
            if (Test-Path -LiteralPath $targetFile) { throw "Refusing to overwrite: $targetFile" }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $targetFile
        }
    }
}
