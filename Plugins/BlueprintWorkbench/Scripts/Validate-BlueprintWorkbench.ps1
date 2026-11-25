param([Parameter(Mandatory)][string]$ProjectFile, [Parameter(Mandatory)][string]$EngineRoot, [switch]$MemoryCache)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/Workbench.Common.ps1"
$context = Get-WorkbenchContext $ProjectFile $EngineRoot -MemoryCache:$MemoryCache
# Creates and edits only uniquely named test assets, never supplied gameplay assets.
foreach ($scriptName in @('TestWorkbench','TestWorkbenchReload','TestAdvancedWorkbench','TestAdvancedWorkbenchReload')) {
    $logFile = Join-Path $context.Reports "$scriptName.log"
    Invoke-WorkbenchPython $context "$PSScriptRoot/$scriptName.py" $logFile
    if (Select-String -LiteralPath $logFile -SimpleMatch 'BackwardCompatibilityNodeConversion Error' -Quiet) {
        throw "Unexpected Blueprint compatibility conversion failure. Log: $logFile"
    }
}
$reportFile = Join-Path $context.Reports 'test-results.json'
$report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
if ($report.run_id -notmatch '^[a-f0-9]{32}$') { throw 'Invalid test run identity.' }
$expected = [IO.Path]::GetFullPath((Join-Path $context.Root "Content/__BlueprintWorkbenchTests/$($report.run_id)/BP_Patched.uasset"))
if ([IO.Path]::GetFullPath($report.fixture_file) -ne $expected) { throw 'Unexpected test cleanup path.' }
Remove-Item -LiteralPath $expected
$report.checks += 'generated test asset removed after round-trip verification'
$advanced = Get-Content -LiteralPath (Join-Path $context.Reports 'advanced-results.json') -Raw | ConvertFrom-Json
if (-not $advanced.success) { throw 'Advanced capability integration did not pass.' }
$report.checks += $advanced.checks
$report | Add-Member -NotePropertyName advanced_run_id -NotePropertyValue $advanced.run_id -Force
$report | Add-Member -NotePropertyName engine_version -NotePropertyValue $context.Version -Force
$report | Add-Member -NotePropertyName project_file -NotePropertyValue $context.Project -Force
$report | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $reportFile -Encoding UTF8
Write-Output "Blueprint Workbench $($context.Version): $($report.checks.Count) checks passed. Report: $reportFile"
