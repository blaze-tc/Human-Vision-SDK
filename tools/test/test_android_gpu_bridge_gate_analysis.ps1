$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'android_gpu_bridge_gate_analysis.ps1')
$uuidA = '11111111111111111111111111111111'
$uuidB = '22222222222222222222222222222222'
$baseEpoch = 1790000000.0
$endEpoch = $baseEpoch + 600
$probe = @"
candidate=blit width=320 height=240 layers=1 format=1 usage=256 stride=320
producer vk_format=37 external_format=0 external_features=1 concrete_features=32769 image_usage=6 optimal_ahb_usage=256 required_standard_ahb_usage=256
consumer vk_format=37 external_format=0 external_features=1 concrete_features=1 image_usage=4 optimal_ahb_usage=256 required_standard_ahb_usage=256
externalMemoryFeatures=2 compatibleHandleTypes=1024 maxExtent=4096x4096
"@
$entries = [System.Collections.Generic.List[object]]::new()
for ($i = 0; $i -le 120; $i++) {
    $orientation = @('Portrait', 'LandscapeLeft', 'LandscapeRight')[$i % 3]
    $count = $i + 1
    $status = "HV_GPU_GATE frame=$count result=0 orientation=$orientation path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1 submitted=$count imported=$count converted=$count unityDeviceUUID=$uuidA ncnnDeviceUUID=$uuidA unityDriverUUID=$uuidB ncnnDriverUUID=$uuidB error=<none>"
    $entries.Add([pscustomobject]@{ epoch = $baseEpoch + 5 * $i; text = $status })
}
foreach ($event in @(
    @(200, 'HV_GPU_GATE pause=True'), @(205, 'HV_GPU_GATE pause=False'),
    @(210, 'HV_GPU_GATE source resumed after=pause'),
    @(250, 'HV_GPU_GATE focus=False'), @(260, 'HV_GPU_GATE focus=True'),
    @(300, 'HV_GPU_GATE camera restart requested'),
    @(305, 'HV_GPU_GATE source resumed after=restart'))) {
    $entries.Add([pscustomobject]@{ epoch = $baseEpoch + $event[0]; text = $event[1] })
}
function Format-Entry($entry) {
    $timestamp = [double]$entry.epoch
    $timestamp.ToString('F3', [Globalization.CultureInfo]::InvariantCulture) + ' I Unity: ' + $entry.text
}
$valid = $probe + "`n" + (($entries | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
function Analyze([string]$raw) {
    Get-AndroidGpuBridgeGateAnalysis -RawLog $raw -DurationMinutes 10 -GateSource 'safe gate source' -CaptureStartEpoch $baseEpoch -CaptureEndEpoch $endEpoch
}
function Assert-Fail([string]$name, [string]$raw, [string]$check = '') {
    $result = Analyze $raw
    if ($result.result -ne 'FAIL') { throw "$name incorrectly passed" }
    if ($check -and $result.checks[$check]) { throw "$name did not fail $check" }
}
$baseline = Analyze $valid
if ($baseline.result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw "Valid fixture failed: $($baseline.checks | ConvertTo-Json -Compress)" }
Assert-Fail 'later error' ($valid + "`n" + (Format-Entry ([pscustomobject]@{epoch=$endEpoch;text='HV_GPU_GATE frame=121 imported=121 converted=121 error=ncnn_import_failed'}))) 'no_status_error'
Assert-Fail 'stalled import' ($valid -replace ' imported=\d+ ', ' imported=1 ') 'gpu_import_observed'
Assert-Fail 'stalled conversion' ($valid -replace ' converted=\d+ ', ' converted=1 ') 'gpu_conversion_observed'
Assert-Fail 'native fatal' ($valid + "`n1790000590.000 E libc: Fatal signal 11 (SIGSEGV)") 'no_native_or_unity_fatal'
Assert-Fail 'crash dump fatal' ($valid + "`n1790000590.000 E crash_dump64: Abort message") 'no_native_or_unity_fatal'
Assert-Fail 'wrong actual AHB usage' ($valid.Replace('ahbUsage=0x100', 'ahbUsage=0x300')) 'selected_path_matches_actual_contract'
Assert-Fail 'wrong producer image usage' ($valid.Replace('image_usage=6 ', 'image_usage=20 ')) 'selected_path_matches_actual_contract'
Assert-Fail 'no orientation evidence' ($valid.Replace('orientation=LandscapeRight', 'orientation=Portrait')) 'portrait_and_both_landscapes'
Assert-Fail 'no pause evidence' ($valid.Replace('HV_GPU_GATE pause=True', 'HV_GPU_GATE no pause')) 'pause_resume'
Assert-Fail 'no restart evidence' ($valid.Replace('HV_GPU_GATE camera restart requested', 'HV_GPU_GATE no restart')) 'camera_restart'
Assert-Fail 'no focus return' ($valid.Replace('HV_GPU_GATE focus=True', 'HV_GPU_GATE no focus return')) 'background_foreground'
Assert-Fail 'no restart recovery' ($valid.Replace('HV_GPU_GATE source resumed after=restart', 'HV_GPU_GATE no restart recovery')) 'pause_and_restart_recovered'
$sparse = $probe + "`n" + (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($baseEpoch + 10) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'ten minute sleep with three early statuses' $sparse 'timestamped_status_coverage'
$missingTail = $probe + "`n" + (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($endEpoch - 60) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'no status near capture end' $missingTail 'timestamped_status_coverage'
$longGap = $probe + "`n" + (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($baseEpoch + 100) -or $_.epoch -ge ($baseEpoch + 500) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'long status gap' $longGap 'timestamped_status_coverage'
$lateStall = $probe + "`n" + (($entries | ForEach-Object {
    if ($_.epoch -ge ($endEpoch - 90) -and $_.text -match ' frame=') {
        [pscustomobject]@{epoch=$_.epoch;text=($_.text -replace ' imported=\d+ converted=\d+ ', ' imported=103 converted=103 ')}
    } else { $_ }
} | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'late import and conversion stall' $lateStall 'imported_and_converted_progress_near_end'
$noRecoveryProgress = $valid.Replace('HV_GPU_GATE source resumed after=restart', 'HV_GPU_GATE no restart recovery') +
    "`n1790000599.000 I Unity: HV_GPU_GATE source resumed after=restart"
Assert-Fail 'restart recovery with no later progress' $noRecoveryProgress 'post_recovery_import_and_conversion_progress'
Assert-Fail 'multiline status error' ($valid.Replace(' error=<none>', " error=`nGPU import failed")) 'no_status_error'
$collectorSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'collect_android_gpu_bridge_gate.ps1') -Raw
if ($collectorSource -match "'Unity:I','AndroidRuntime:E','\*:S'" -or $collectorSource -notmatch "'crash'") {
    throw 'Collector filters out native crash records'
}
Write-Output 'Android GPU bridge gate analyzer: 19/19 PASS'
