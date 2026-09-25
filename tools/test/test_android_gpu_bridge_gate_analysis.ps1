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
    $generation = if ($i -le 41) { 1 } elseif ($i -le 60) { 2 } else { 3 }
    $status = "HV_GPU_GATE frame=$count generation=$generation result=0 orientation=$orientation path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1 submitted=$count imported=$count converted=$count unityDeviceUUID=$uuidA ncnnDeviceUUID=$uuidA unityDriverUUID=$uuidB ncnnDriverUUID=$uuidB error=<none>"
    $entries.Add([pscustomobject]@{ epoch = $baseEpoch + [Math]::Max(0.02, 5 * $i); text = $status })
}
foreach ($event in @(
    @(0, 'HV_GPU_GATE source rebuilding generation=1 width=320 height=240 rotation=90 mirror=False'),
    @(200, 'HV_GPU_GATE pause=True'), @(205, 'HV_GPU_GATE pause=False'),
    @(210, 'HV_GPU_GATE source resumed after=pause'),
    @(209, 'HV_GPU_GATE source rebuilding generation=2 width=320 height=240 rotation=90 mirror=False'),
    @(250, 'HV_GPU_GATE focus=False'), @(260, 'HV_GPU_GATE focus=True'),
    @(300, 'HV_GPU_GATE camera restart requested'),
    @(305, 'HV_GPU_GATE source resumed after=restart'),
    @(304, 'HV_GPU_GATE source rebuilding generation=3 width=320 height=240 rotation=90 mirror=False'))) {
    $entries.Add([pscustomobject]@{ epoch = $baseEpoch + $event[0]; text = $event[1] })
}
foreach ($generation in 1..3) {
    $probeEpoch = $baseEpoch + @{ 1 = 0.001; 2 = 209.001; 3 = 304.001 }[$generation]
    $probeLineIndex = 0
    foreach ($line in ($probe -split "`r?`n" | Where-Object { $_ })) {
        $entries.Add([pscustomobject]@{ epoch = $probeEpoch + (0.001 * $probeLineIndex++); text = "HV_GPU_GATE probe generation=$generation $line" })
    }
}
function Format-Entry($entry) {
    $timestamp = [double]$entry.epoch
    $timestamp.ToString('F3', [Globalization.CultureInfo]::InvariantCulture) + ' I Unity: ' + $entry.text
}
$valid = ($entries | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n"
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
$unityLandscapeAlias = $valid.Replace('orientation=LandscapeLeft', 'orientation=Landscape')
if ((Analyze $unityLandscapeAlias).result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') {
    throw 'Unity Landscape alias for LandscapeLeft incorrectly failed'
}
$staleProbe = ($valid -split "`r?`n" | Where-Object { $_ -notmatch 'HV_GPU_GATE probe generation=3 ' }) -join "`n"
Assert-Fail 'single stale probe reused after restart' $staleProbe 'probe_for_each_configured_generation'
$missingConsumer = ($valid -split "`r?`n" | Where-Object { $_ -notmatch 'HV_GPU_GATE probe generation=3 consumer vk_format=' }) -join "`n"
Assert-Fail 'restart probe missing consumer measurement' $missingConsumer 'probe_for_each_configured_generation'
$lateGenerationCandidate = @($valid -split "`r?`n" | Where-Object { $_ -match 'HV_GPU_GATE probe generation=3 candidate=' })[0]
$earlyGenerationCandidate = $lateGenerationCandidate -replace '^\s*\d{10}(?:\.\d+)?', '1790000303.000'
Assert-Fail 'new-generation probe predates rebuild' ($valid.Replace($lateGenerationCandidate, $earlyGenerationCandidate)) 'probe_for_each_configured_generation'
Assert-Fail 'duplicate selected candidate probe' ($valid + "`n" + $lateGenerationCandidate) 'probe_for_each_configured_generation'
$pending = "HV_GPU_GATE frame=0 generation=1 result=1 orientation=Portrait path=0 ahbFormat=0 ahbUsage=0x0 formatFeatures=0x0 submitted=0 imported=0 converted=0 unityDeviceUUID=$('0' * 32) ncnnDeviceUUID=$('0' * 32) unityDriverUUID=$('0' * 32) ncnnDriverUUID=$('0' * 32) error=<none>"
$withPending = (Format-Entry ([pscustomobject]@{epoch=$baseEpoch; text=$pending})) + "`n" + $valid
if ((Analyze $withPending).result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw 'Pending source probe incorrectly failed' }
$afterRestartPending = $pending.Replace('frame=0 generation=1', 'frame=62 generation=3')
$withRestartPending = (@($entries) + @([pscustomobject]@{epoch=$baseEpoch + 304.5; text=$afterRestartPending}) | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n"
if ((Analyze $withRestartPending).result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw 'Pending measurement after camera restart incorrectly failed' }
$smoke = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'fixtures/android_gpu_gate_smoke_status.log') -Raw
$smokeResult = Get-AndroidGpuBridgeGateAnalysis -RawLog $smoke -DurationMinutes 10 -GateSource 'safe gate source' -CaptureStartEpoch 1790316286.8 -CaptureEndEpoch 1790316287.7
foreach ($check in @('pending_source_measurements_recover', 'selected_copy_path', 'exact_nonzero_device_and_driver_uuids', 'no_status_error')) {
    if (!$smokeResult.checks[$check]) { throw "Real smoke excerpt failed $check" }
}
if ($smokeResult.checks.timestamped_status_coverage) { throw 'Short real smoke excerpt incorrectly met ten-minute coverage' }
$colorFallback = $valid.Replace('candidate=blit width=320 height=240 layers=1 format=1 usage=256', 'candidate=color_attachment width=320 height=240 layers=1 format=1 usage=768').Replace('image_usage=6 ', 'image_usage=20 ').Replace(' path=1 ', ' path=2 ').Replace('ahbUsage=0x100', 'ahbUsage=0x300')
$colorFallback = $colorFallback.Replace('HV_GPU_GATE probe generation=1 candidate=color_attachment', "HV_GPU_GATE probe generation=1 failed: source.blit_src`n1790000000.001 I Unity: HV_GPU_GATE probe generation=1 candidate=color_attachment")
if ((Analyze $colorFallback).result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw 'Successful color fallback after rejected blit incorrectly failed' }
Assert-Fail 'healthy probe in status error' ($valid.Replace(' error=<none>', ' error=\ncandidate=blit width=320')) 'no_status_error'
$spurious = @($valid -split "`r?`n" | Where-Object { $_ -match ' frame=81 ' })[0]
$spuriousPending = $spurious.Replace('result=0', 'result=1').Replace('path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1', 'path=0 ahbFormat=0 ahbUsage=0x0 formatFeatures=0x0').Replace($uuidA, '0' * 32).Replace($uuidB, '0' * 32)
Assert-Fail 'later path-zero regression without source change' ($valid.Replace($spurious, $spuriousPending)) 'pending_source_measurements_recover'
Assert-Fail 'non-pending path-zero regression' ($valid.Replace($spurious, $spurious.Replace('path=1', 'path=0'))) 'pending_source_measurements_recover'
Assert-Fail 'later error' ($valid + "`n" + (Format-Entry ([pscustomobject]@{epoch=$endEpoch;text='HV_GPU_GATE frame=121 imported=121 converted=121 error=ncnn_import_failed'}))) 'no_status_error'
Assert-Fail 'stalled import' ($valid -replace ' imported=\d+ ', ' imported=1 ') 'gpu_import_observed'
Assert-Fail 'stalled conversion' ($valid -replace ' converted=\d+ ', ' converted=1 ') 'gpu_conversion_observed'
Assert-Fail 'native fatal' ($valid + "`n1790000590.000 E libc: Fatal signal 11 (SIGSEGV)") 'no_native_or_unity_fatal'
Assert-Fail 'crash dump fatal' ($valid + "`n1790000590.000 E crash_dump64: Abort message") 'no_native_or_unity_fatal'
Assert-Fail 'gate submit exception' ($valid + "`n1790000590.000 E Unity: InvalidOperationException: Gate submit failed: 4") 'no_native_or_unity_fatal'
Assert-Fail 'gate lease exception' ($valid + "`n1790000590.000 E Unity: InvalidOperationException: Gate source lease failed") 'no_native_or_unity_fatal'
Assert-Fail 'gate startup error' ($valid + "`n1790000590.000 E Unity: Gate requires Vulkan") 'no_native_or_unity_fatal'
Assert-Fail 'gate exception on info tag' ($valid + "`n1790000590.000 I Unity: InvalidOperationException: Gate render event unavailable") 'no_native_or_unity_fatal'
Assert-Fail 'wrong actual AHB usage' ($valid.Replace('ahbUsage=0x100', 'ahbUsage=0x300')) 'selected_path_matches_actual_contract'
Assert-Fail 'wrong producer image usage' ($valid.Replace('image_usage=6 ', 'image_usage=20 ')) 'selected_path_matches_actual_contract'
Assert-Fail 'no left landscape evidence' ($valid.Replace('orientation=LandscapeLeft', 'orientation=Portrait')) 'portrait_and_both_landscapes'
Assert-Fail 'no orientation evidence' ($valid.Replace('orientation=LandscapeRight', 'orientation=Portrait')) 'portrait_and_both_landscapes'
Assert-Fail 'no pause evidence' ($valid.Replace('HV_GPU_GATE pause=True', 'HV_GPU_GATE no pause')) 'pause_resume'
Assert-Fail 'no restart evidence' ($valid.Replace('HV_GPU_GATE camera restart requested', 'HV_GPU_GATE no restart')) 'camera_restart'
Assert-Fail 'no focus return' ($valid.Replace('HV_GPU_GATE focus=True', 'HV_GPU_GATE no focus return')) 'background_foreground'
Assert-Fail 'no restart recovery' ($valid.Replace('HV_GPU_GATE source resumed after=restart', 'HV_GPU_GATE no restart recovery')) 'pause_and_restart_recovered'
$sparse = (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($baseEpoch + 10) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'ten minute sleep with three early statuses' $sparse 'timestamped_status_coverage'
$missingTail = (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($endEpoch - 60) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'no status near capture end' $missingTail 'timestamped_status_coverage'
$longGap = (($entries | Where-Object { $_.text -notmatch ' frame=' -or $_.epoch -le ($baseEpoch + 100) -or $_.epoch -ge ($baseEpoch + 500) } | Sort-Object epoch | ForEach-Object { Format-Entry $_ }) -join "`n")
Assert-Fail 'long status gap' $longGap 'timestamped_status_coverage'
$lateStall = (($entries | ForEach-Object {
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
$warning = Analyze ($valid + "`n1790000590.000 W Unity: harmless texture warning")
if ($warning.result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw 'Harmless Unity warning incorrectly failed' }
Write-Output 'Android GPU bridge gate analyzer: 37/37 PASS'
