param([Parameter(Mandatory)][string]$ProjectFile, [Parameter(Mandatory)][string]$EngineRoot)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$context = Get-WorkbenchContext $ProjectFile $EngineRoot
$common = @{ ProjectFile=$context.Project; EngineRoot=$context.Engine }
$checks = @()
Invoke-WorkbenchPython $context "$PSScriptRoot/TestCliSetup.py" (Join-Path $context.Reports 'cli-setup.log')
$manifest = Get-Content -LiteralPath (Join-Path $context.Reports 'cli-manifest.json') -Raw | ConvertFrom-Json
$inspectReport = Join-Path $context.Reports 'cli-inspect.json'
& "$PSScriptRoot/Invoke-BlueprintWorkbench.ps1" @common -AssetPath $manifest.source -ReportFile $inspectReport
$inspection = Get-Content -LiteralPath $inspectReport -Raw | ConvertFrom-Json
if (-not $inspection.success -or $inspection.compiled -or $inspection.saved) { throw 'CLI inspect contract failed.' }
$checks += 'CLI inspect selects supplied project and reads without compilation or saving'
$previewReport = Join-Path $context.Reports 'cli-preview.json'
& "$PSScriptRoot/Invoke-BlueprintWorkbench.ps1" @common -AssetPath $manifest.source -PatchFile "$PSScriptRoot/example.patch.json" -ReportFile $previewReport
$preview = Get-Content -LiteralPath $previewReport -Raw | ConvertFrom-Json
if (-not $preview.success -or -not $preview.compiled -or $preview.saved) { throw 'CLI preview contract failed.' }
$checks += 'CLI patch defaults to compiled in-memory preview'
& "$PSScriptRoot/Invoke-BlueprintWorkbench.ps1" @common -AssetPath $manifest.source -PatchFile "$PSScriptRoot/example.patch.json" -DestinationPackage $manifest.temporary -TemporaryCopy -ReportFile (Join-Path $context.Reports 'cli-temporary.json')
$checks += 'CLI saves and registers explicitly marked temporary review copy'
$writeReport = Join-Path $context.Reports 'cli-writeback.json'
& "$PSScriptRoot/Invoke-BlueprintWorkbench.ps1" @common -AssetPath $manifest.source -PatchFile "$PSScriptRoot/example.patch.json" -WriteBack -ReportFile $writeReport
$written = Get-Content -LiteralPath $writeReport -Raw | ConvertFrom-Json
$finalized = Get-Content -LiteralPath "$writeReport.finalize.json" -Raw | ConvertFrom-Json
if (-not $written.saved -or -not (Test-Path -LiteralPath $written.backup_file) -or -not $finalized.success -or @($finalized.deleted).Count -ne 1 -or $finalized.deleted[0] -ne $manifest.temporary) { throw 'CLI writeback/finalize contract failed.' }
$checks += 'CLI explicit writeback saves with backup and independently reloads and recompiles'
$checks += 'CLI finalization deletes exactly the matching registered unreferenced temporary copy'
$spec = @{graph='EventGraph';groups=@(@{entry=$written.aliases.start;nodes=@($written.aliases.start,$written.aliases.set_flag);title='CLI test';details='Keep execution and defaults'});candidates=@()}
$specFile = Join-Path $context.Reports 'cli-presentation-spec.json'
$spec | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $specFile -Encoding UTF8
$layoutReport = Join-Path $context.Reports 'cli-layout.json'
& "$PSScriptRoot/Invoke-BlueprintWorkbench.ps1" @common -AssetPath $manifest.source -PresentationFile $specFile -WriteBack -ReportFile $layoutReport
$layout = Get-Content -LiteralPath $layoutReport -Raw | ConvertFrom-Json
$layoutFinalized = Get-Content -LiteralPath "$layoutReport.finalize.json" -Raw | ConvertFrom-Json
if (-not $layout.saved -or -not $layoutFinalized.success -or @($layoutFinalized.deleted).Count -ne 0) { throw 'CLI layout/finalize contract failed.' }
$checks += 'CLI presentation preserves logic, saves layout and comment, and independently reloads'
Invoke-WorkbenchPython $context "$PSScriptRoot/TestCliCleanup.py" (Join-Path $context.Reports 'cli-cleanup.log')
$checks += 'CLI test cleanup removes only its recorded source fixture and retains backup and reports'
@{success=$true;engine_version=$context.Version;project_file=$context.Project;checks=$checks} | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath (Join-Path $context.Reports 'test-cli-results.json') -Encoding UTF8
Write-Output "CLI $($context.Version): $($checks.Count) checks passed."
