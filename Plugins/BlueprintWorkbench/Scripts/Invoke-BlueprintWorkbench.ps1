param(
    [Parameter(Mandatory)][string]$ProjectFile,
    [string]$AssetPath,
    [ValidateSet('normal','function_library','interface')][string]$CreateKind,
    [string]$ParentClass = '/Script/Engine.Actor',
    [string]$MemberName,
    [string]$PatchFile,
    [string]$PresentationFile,
    [string]$DestinationPackage,
    [switch]$WriteBack,
    [switch]$TemporaryCopy,
    [string]$ReportFile,
    [Parameter(Mandatory)][string]$EngineRoot,
    [switch]$MemoryCache
)
$ErrorActionPreference = 'Stop'
if ($CreateKind -and ($AssetPath -or $WriteBack -or $PresentationFile -or $TemporaryCopy -or $MemberName)) { throw 'CreateKind cannot combine with existing-asset operations.' }
if (-not $CreateKind -and -not $AssetPath) { throw 'AssetPath is required unless CreateKind is provided.' }
if ($MemberName -and ($PatchFile -or $PresentationFile -or $DestinationPackage -or $WriteBack)) { throw 'MemberName is a read-only reference query.' }
if ($PatchFile -and $PresentationFile) { throw 'Use PatchFile or PresentationFile, not both.' }
if ($WriteBack -and ((-not $PatchFile -and -not $PresentationFile) -or $DestinationPackage)) { throw 'WriteBack requires PatchFile or PresentationFile and cannot use DestinationPackage.' }
if ($TemporaryCopy -and ((-not $PatchFile -and -not $PresentationFile) -or -not $DestinationPackage -or $WriteBack -or $DestinationPackage -notmatch '^/Game/Workbench/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+$')) { throw 'TemporaryCopy requires an edit specification and a new destination under /Game/Workbench/.' }
if ($WriteBack -and @(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) { throw 'Save work and close interactive Unreal editors before writeback.' }
. "$PSScriptRoot/Workbench.Common.ps1"
$context = Get-WorkbenchContext $ProjectFile $EngineRoot -MemoryCache:$MemoryCache
$editorArguments = $context.EditorArguments
$scriptRoot = $PSScriptRoot.Replace('\','/')
$projectRoot = $context.Root
$runId = [Guid]::NewGuid().ToString('N')
$outputDirectory = Join-Path $projectRoot 'Saved/BlueprintWorkbench'
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
if (-not $ReportFile) { $ReportFile = Join-Path $outputDirectory "$runId-result.json" }
$ReportFile = [IO.Path]::GetFullPath($ReportFile)
$request = @{ asset = $AssetPath; report = $ReportFile; action = 'inspect' }
if ($PatchFile -or $PresentationFile) {
    $request.action = 'patch'
    if ($WriteBack) { $request.action = 'writeback' }
    if ($PatchFile) { $request.patch = Get-Content -LiteralPath $PatchFile -Raw | ConvertFrom-Json }
    if ($PresentationFile) { $request.presentation = Get-Content -LiteralPath $PresentationFile -Raw | ConvertFrom-Json }
    if ($DestinationPackage) { $request.destination = $DestinationPackage }
    if ($TemporaryCopy) { $request.temporary_copy = $true }
} elseif ($DestinationPackage -and -not $CreateKind) { throw 'DestinationPackage requires PatchFile.' }
if ($CreateKind) { $request.action = 'create'; $request.kind = $CreateKind; $request.parent = $ParentClass; if (-not $request.patch) { $request.patch = @{operations=@()} }; if ($DestinationPackage) { $request.destination = $DestinationPackage } }
if ($MemberName) { $request.action = 'references'; $request.member = $MemberName }
$requestFile = Join-Path $outputDirectory "$runId-request.json"
$request | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $requestFile -Encoding UTF8
$oldRequest = $env:BLUEPRINT_WORKBENCH_REQUEST
try {
    $env:BLUEPRINT_WORKBENCH_REQUEST = $requestFile
    & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') `
        $context.Project -run=pythonscript `
        "-script=$scriptRoot/RunWorkbench.py" -unattended -nop4 -NullRHI -nosound `
        "-abslog=$outputDirectory/$runId.log" @editorArguments
    $processExit = $LASTEXITCODE
    if ($processExit -eq 0 -and $CreateKind -and $DestinationPackage) {
        & $context.Editor $context.Project -run=pythonscript "-script=$scriptRoot/FinalizeCreatedWorkbench.py" -unattended -nop4 -NullRHI -nosound "-abslog=$outputDirectory/$runId-finalize.log" @editorArguments
        $processExit = $LASTEXITCODE
    }
    if ($processExit -eq 0 -and $WriteBack) {
        & (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') `
            $context.Project -run=pythonscript `
            "-script=$scriptRoot/FinalizeWorkbench.py" -unattended -nop4 -NullRHI -nosound `
            "-abslog=$outputDirectory/$runId-finalize.log" @editorArguments
        $processExit = $LASTEXITCODE
    }
} finally { $env:BLUEPRINT_WORKBENCH_REQUEST = $oldRequest }
if ($processExit -ne 0) { throw "UE failed ($processExit). Report: $ReportFile; log: $outputDirectory/$runId.log" }
if (-not (Test-Path -LiteralPath $ReportFile)) { throw 'UE did not write a report.' }
$result = Get-Content -LiteralPath $ReportFile -Raw | ConvertFrom-Json
if (-not $result.success) { throw "Workbench failed: $($result.error). Report: $ReportFile" }
if ($WriteBack) {
    $cleanup = Get-Content -LiteralPath "$ReportFile.finalize.json" -Raw | ConvertFrom-Json
    Write-Output "Verified originals; deleted $($cleanup.deleted.Count) temporary copies. Cleanup report: $ReportFile.finalize.json"
    if (-not $cleanup.cleanup_complete) { Write-Warning 'Referenced temporary copies were retained; see the cleanup report.' }
}
Write-Output "Success. Compiled=$($result.compiled); Saved=$($result.saved); Report=$ReportFile"
