$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'android_gpu_bridge_gate_analysis.ps1')
$uuidA = '11111111111111111111111111111111'
$uuidB = '22222222222222222222222222222222'
$probe = @"
candidate=blit width=320 height=240 layers=1 format=1 usage=256 stride=320
producer vk_format=37 external_format=0 external_features=1 concrete_features=32769 image_usage=6 optimal_ahb_usage=256 required_standard_ahb_usage=256
consumer vk_format=37 external_format=0 external_features=1 concrete_features=1 image_usage=4 optimal_ahb_usage=256 required_standard_ahb_usage=256
externalMemoryFeatures=2 compatibleHandleTypes=1024 maxExtent=4096x4096
"@
$rows = @(
  "HV_GPU_GATE frame=1 result=0 orientation=Portrait path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1 submitted=1 imported=1 converted=1 unityDeviceUUID=$uuidA ncnnDeviceUUID=$uuidA unityDriverUUID=$uuidB ncnnDriverUUID=$uuidB error=",
  "HV_GPU_GATE frame=2 result=0 orientation=LandscapeLeft path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1 submitted=2 imported=2 converted=2 unityDeviceUUID=$uuidA ncnnDeviceUUID=$uuidA unityDriverUUID=$uuidB ncnnDriverUUID=$uuidB error=",
  "HV_GPU_GATE frame=3 result=0 orientation=LandscapeRight path=1 ahbFormat=1 ahbUsage=0x100 formatFeatures=0x1 submitted=3 imported=3 converted=3 unityDeviceUUID=$uuidA ncnnDeviceUUID=$uuidA unityDriverUUID=$uuidB ncnnDriverUUID=$uuidB error=",
  'HV_GPU_GATE pause=True', 'HV_GPU_GATE pause=False', 'HV_GPU_GATE focus=False', 'HV_GPU_GATE focus=True',
  'HV_GPU_GATE camera restart requested', 'HV_GPU_GATE source resumed after=pause', 'HV_GPU_GATE source resumed after=restart')
$valid = $probe + "`n" + ($rows -join "`n")
function Assert-Fail([string]$name, [string]$raw) {
  $result = Get-AndroidGpuBridgeGateAnalysis -RawLog $raw -DurationMinutes 10 -GateSource 'safe gate source'
  if ($result.result -ne 'FAIL') { throw "$name incorrectly passed" }
}
$baseline = Get-AndroidGpuBridgeGateAnalysis -RawLog $valid -DurationMinutes 10 -GateSource 'safe gate source'
if ($baseline.result -ne 'PASS_CANDIDATE_REQUIRES_USER_REVIEW') { throw "Valid fixture failed: $($baseline.checks | ConvertTo-Json -Compress)" }
Assert-Fail 'later error' ($valid + "`n" + $rows[0].Replace(' error=', ' error=ncnn_import_failed'))
Assert-Fail 'stalled import' ($valid.Replace(' imported=2 ', ' imported=1 ').Replace(' imported=3 ', ' imported=1 '))
Assert-Fail 'stalled conversion' ($valid.Replace(' converted=2 ', ' converted=1 ').Replace(' converted=3 ', ' converted=1 '))
Assert-Fail 'native fatal' ($valid + "`nFatal signal 11 (SIGSEGV)")
Assert-Fail 'wrong actual AHB usage' ($valid.Replace('ahbUsage=0x100', 'ahbUsage=0x300'))
Assert-Fail 'wrong producer image usage' ($valid.Replace('image_usage=6 ', 'image_usage=20 '))
Assert-Fail 'no orientation evidence' ($valid.Replace('orientation=LandscapeRight', 'orientation=Portrait'))
Assert-Fail 'no pause evidence' ($valid.Replace('HV_GPU_GATE pause=True', 'HV_GPU_GATE no pause'))
Assert-Fail 'no restart evidence' ($valid.Replace('HV_GPU_GATE camera restart requested', 'HV_GPU_GATE no restart'))
Assert-Fail 'no focus return' ($valid.Replace('HV_GPU_GATE focus=True', 'HV_GPU_GATE no focus return'))
Assert-Fail 'no restart recovery' ($valid.Replace('HV_GPU_GATE source resumed after=restart', 'HV_GPU_GATE no restart recovery'))
Write-Output 'Android GPU bridge gate analyzer: 12/12 PASS'
