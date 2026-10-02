function New-InputGateProcessIdentity($process, [string]$launchedExe) {
  $resolvedExe=(Get-Item -LiteralPath $launchedExe -ErrorAction Stop).FullName
  return @{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;exe=$resolvedExe;initialCapturedPath=$process.Path}
}

function Stop-InputGateOwnedProcess($identity) {
  $live=Get-Process -Id $identity.pid -ErrorAction SilentlyContinue
  if(-not $live){return 'already_exited'}
  $liveExe=$live.Path
  if(-not $liveExe){$liveExe=(Get-CimInstance Win32_Process -Filter "ProcessId=$($identity.pid)" -ErrorAction Stop).ExecutablePath}
  if($live.StartTime.ToUniversalTime().Ticks -ne $identity.startTicks -or -not $liveExe -or $liveExe -ne $identity.exe){throw "Owned process identity mismatch: $($identity.pid); process untouched"}
  Stop-Process -Id $live.Id -Force -ErrorAction Stop
  if(-not $live.WaitForExit(5000)){throw "Owned process termination timed out: $($identity.pid)"}
  return 'terminated'
}
