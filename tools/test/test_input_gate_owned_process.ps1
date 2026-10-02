$ErrorActionPreference='Stop'
. "$PSScriptRoot/input_gate_owned_process.ps1"
$exe=(Get-Process -Id $PID).Path
$failures=0
foreach($case in @('initial_path_null','mismatched_start_ticks','mismatched_path','normal_retirement')) {
  $child=Start-Process -FilePath $exe -ArgumentList '-NoProfile -Command "Start-Sleep -Seconds 120"' -WindowStyle Hidden -PassThru
  try {
    $deadline=[DateTime]::UtcNow.AddSeconds(3)
    do {$live=Get-Process -Id $child.Id; if($live.Path){break};Start-Sleep -Milliseconds 20} while([DateTime]::UtcNow -lt $deadline)
    if(-not $live.Path){throw 'Real child executable path did not become observable'}
    $initial=[pscustomobject]@{Id=$child.Id;StartTime=$child.StartTime;Path=$(if($case -eq 'initial_path_null'){$null}else{$exe})}
    $identity=New-InputGateProcessIdentity $initial $exe
    if($case -eq 'mismatched_start_ticks'){$identity.startTicks++}
    if($case -eq 'mismatched_path'){$identity.exe='C:\incorrect-owned-executable.exe'}
    if($case -like 'mismatched*') {
      $rejected=$false
      try {Stop-InputGateOwnedProcess $identity | Out-Null} catch {$rejected=$_.Exception.Message -match 'identity mismatch'}
      if(-not $rejected -or -not(Get-Process -Id $child.Id -ErrorAction SilentlyContinue)){throw 'Mismatched child was not preserved'}
    } else {
      if((Stop-InputGateOwnedProcess $identity) -ne 'terminated' -or (Get-Process -Id $child.Id -ErrorAction SilentlyContinue)){throw 'Owned child did not actually terminate'}
      if((Stop-InputGateOwnedProcess $identity) -ne 'already_exited'){throw 'Repeated cleanup did not recognize absence'}
    }
    Write-Output "PASS $case"
  } catch {$failures++;Write-Output "FAIL ${case}: $($_.Exception.Message)"}
  finally {if(Get-Process -Id $child.Id -ErrorAction SilentlyContinue){Stop-Process -Id $child.Id -Force;$child.WaitForExit()}}
}
if($failures){throw "$failures owned process cleanup tests failed"}
