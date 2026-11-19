param([Parameter(Mandatory)][string]$OutputFile)
$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath($OutputFile)
if (Test-Path -LiteralPath $output) { throw "Output already exists: $output" }
if ([IO.Path]::GetExtension($output) -ne '.zip') { throw 'OutputFile must end in .zip.' }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $output) | Out-Null
$sourceRoot = Split-Path -Parent $PSScriptRoot
$archive = [IO.Compression.ZipFile]::Open($output, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($entry in @('BlueprintWorkbench.uplugin','Source','Scripts','Docs','README.md','AGENTS.md')) {
        $path = Join-Path $sourceRoot $entry
        $files = if (Test-Path -LiteralPath $path -PathType Container) { Get-ChildItem -LiteralPath $path -File -Recurse } else { Get-Item -LiteralPath $path }
        foreach ($file in $files) {
            if ($file.FullName -match '[\\/]__pycache__[\\/]' -or $file.Extension -eq '.pyc') { continue }
            $relative = [IO.Path]::GetRelativePath($sourceRoot, $file.FullName).Replace('\','/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, "BlueprintWorkbench/$relative") | Out-Null
        }
    }
} finally { $archive.Dispose() }
Write-Output "Source package: $output"
