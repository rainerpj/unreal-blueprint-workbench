param(
    [Parameter(Mandatory)][string]$ProjectFile,
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][string]$AssetPath,
    [Parameter(Mandatory)][string]$GraphPath,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [switch]$WriteBack,
    [string]$PreviewDirectory
)
$ErrorActionPreference = 'Stop'
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { throw 'Save and close existing UE editors before the isolated bridge session.' }
if ($WriteBack -and -not $PreviewDirectory) { throw 'WriteBack requires a successful PreviewDirectory.' }
$ProjectFile = (Resolve-Path -LiteralPath $ProjectFile).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a new output directory; existing evidence is never overwritten.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$request = @{asset=$AssetPath;graph=$GraphPath;output=$OutputDirectory;writeback=[bool]$WriteBack;preview=$PreviewDirectory}
$requestFile = Join-Path $OutputDirectory 'request.json'
$request | ConvertTo-Json | Set-Content -LiteralPath $requestFile -Encoding utf8
$prior = $env:WORKBENCH_ASSIST_REQUEST
$env:WORKBENCH_ASSIST_REQUEST = $requestFile
try {
    $editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe'
    $script = (Join-Path $PSScriptRoot 'RunAssist.py').Replace('\','/')
    $log = Join-Path $OutputDirectory 'editor.log'
    # Slate runs offscreen, with real widget layout. No commandlet/NullRHI fallback.
    $arguments = @('"'+$ProjectFile+'"', '/Engine/Maps/Entry', '-RenderOffscreen', '-unattended', '-nop4', '-nosound', '-NoSplash', '-NoLiveCoding', '-NoRestoreOpenAssetTabs', '-NoLoadStartupPackages', '-ExecCmds="py '+$script+'"', '-abslog="'+$log+'"')
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    while (-not $process.WaitForExit(1000)) {
        if ([DateTime]::UtcNow -gt $deadline) { throw "Owned editor did not exit. PID=$($process.Id). Inspect $log; no forced termination performed." }
    }
    if ($process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode). Evidence: $OutputDirectory" }
    $result = Get-Content -LiteralPath (Join-Path $OutputDirectory 'result.json') -Raw | ConvertFrom-Json
    if (-not $result.success) { throw "Bridge failed: $($result.error). Evidence: $OutputDirectory" }
    if ($WriteBack) {
        & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $ProjectFile -run=pythonscript "-script=$PSScriptRoot/FinalizeAssist.py" -unattended -nop4 -NullRHI -nosound "-abslog=$OutputDirectory/finalize.log" *> "$OutputDirectory/finalize-console.log"
        if ($LASTEXITCODE -ne 0) { throw "Saved asset reload verification failed: $OutputDirectory" }
    }
    Write-Output "Blueprint Assist complete. Saved=$($result.saved). Evidence: $OutputDirectory"
} finally { $env:WORKBENCH_ASSIST_REQUEST = $prior }
