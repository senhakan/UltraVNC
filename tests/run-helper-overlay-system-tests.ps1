param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string]$Report
)
$ErrorActionPreference = 'Stop'
$qaExecutable = (Resolve-Path $Executable).Path
$qaReport = [System.IO.Path]::GetFullPath($Report)
if (Test-Path $qaReport) { throw 'Use a fresh report path; existing reports are not overwritten.' }
$qaTaskName = 'AppCenter-Overlay-QA-' + [guid]::NewGuid().ToString('N')
$qaAction = New-ScheduledTaskAction -Execute $qaExecutable -Argument ('--self-test --report "' + $qaReport + '"')
$qaPrincipal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
$qaSettings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
try {
    Register-ScheduledTask -TaskName $qaTaskName -Action $qaAction -Principal $qaPrincipal -Settings $qaSettings | Out-Null
    Start-ScheduledTask -TaskName $qaTaskName
    $qaDeadline = (Get-Date).AddSeconds(90)
    do {
        Start-Sleep -Milliseconds 250
        $qaInfo = Get-ScheduledTaskInfo -TaskName $qaTaskName
        $qaState = (Get-ScheduledTask -TaskName $qaTaskName).State
        $qaRan = $qaInfo.LastRunTime.Year -gt 2000
    } while ((-not $qaRan -or $qaState -eq 'Running' -or $qaState -eq 'Queued') -and (Get-Date) -lt $qaDeadline)
    if (-not $qaRan -or $qaState -eq 'Running' -or $qaState -eq 'Queued') { throw 'SYSTEM QA timed out.' }
    if (Test-Path $qaReport) { Get-Content $qaReport }
    if ($qaInfo.LastTaskResult -ne 0) { throw ('SYSTEM QA failed: ' + $qaInfo.LastTaskResult) }
    if (-not (Test-Path $qaReport) -or -not (Select-String -Path $qaReport -SimpleMatch '"self_test":"PASS"' -Quiet)) { throw 'SYSTEM QA success report missing.' }
} finally {
    Stop-ScheduledTask -TaskName $qaTaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $qaTaskName -Confirm:$false -ErrorAction SilentlyContinue
}
