param(
    [ValidateRange(10, 120)][int]$DurationMinutes = 10,
    [string]$OutputPath = 'out/device-gates/milestone-b',
    [string]$Apk = 'out/android-gpu-gate-runtime/humanvision-gpu-bridge-gate.apk',
    [string]$Adb = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe',
    [string]$Serial,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$apkPath = (Resolve-Path (Join-Path $root $Apk)).Path
$output = Join-Path $root $OutputPath
New-Item -ItemType Directory -Path $output -Force | Out-Null
$apkHash = (Get-FileHash -LiteralPath $apkPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($DryRun) {
    Write-Output "DRY RUN: APK $apkPath SHA-256 $apkHash; device collection would run for $DurationMinutes minutes into $output. No ADB command executed."
    return
}
$target = @()
if ($Serial) { $target = @('-s', $Serial) }
else {
    $devices = @(& $Adb devices | Select-String '\sdevice$')
    if ($devices.Count -ne 1) { throw "Connect exactly one authorized Android device or pass -Serial; found $($devices.Count)" }
    $Serial = ($devices[0].ToString() -split '\s+')[0]
    $target = @('-s', $Serial)
}
$commit = (git -C $root rev-parse HEAD).Trim()
$device = (& $Adb @target shell getprop ro.product.model).Trim()
$android = (& $Adb @target shell getprop ro.build.version.release).Trim()
$build = (& $Adb @target shell getprop ro.build.fingerprint).Trim()
$driver = (& $Adb @target shell getprop ro.hardware.vulkan).Trim()
& $Adb @target install -r $apkPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'ADB APK install failed' }
& $Adb @target logcat -c -b all
if ($LASTEXITCODE -ne 0) { throw 'ADB logcat clear failed' }
$started = [DateTime]::UtcNow
& $Adb @target shell am start -n 'com.DefaultCompany.UnityProject/com.unity3d.player.UnityPlayerActivity' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Gate activity launch failed' }
$log = Join-Path $output 'logcat.txt'
$capture = Start-Process -FilePath $Adb -ArgumentList (@($target) + @('logcat','-b','main','-b','system','-b','crash','-v','epoch')) -WindowStyle Hidden -RedirectStandardOutput $log -PassThru
Write-Output "Capturing $DurationMinutes minutes from $device ($Serial). Rotate portrait / landscape-left / landscape-right, pause and resume, background and foreground, then tap Restart camera in the gate UI."
try { Start-Sleep -Seconds ($DurationMinutes * 60) }
finally {
    if (!$capture.HasExited) { Stop-Process -Id $capture.Id -Force }
}
$ended = [DateTime]::UtcNow
$raw = Get-Content -LiteralPath $log -Raw
$gateSource = Get-Content -LiteralPath (Join-Path $root 'unity/HumanVisionDemo/Assets/HumanVision/Demo/Live/HumanVisionAndroidGpuGate.cs') -Raw
. (Join-Path $PSScriptRoot 'android_gpu_bridge_gate_analysis.ps1')
$analysis = Get-AndroidGpuBridgeGateAnalysis -RawLog $raw -DurationMinutes ($ended - $started).TotalMinutes -GateSource $gateSource -CaptureStartEpoch (([DateTimeOffset]$started).ToUnixTimeMilliseconds() / 1000.0) -CaptureEndEpoch (([DateTimeOffset]$ended).ToUnixTimeMilliseconds() / 1000.0)
$probe = @($raw -split "`r?`n" | Where-Object { $_ -match 'candidate=|producer vk_format=|consumer vk_format=|externalMemoryFeatures=|failed:' } | Select-Object -Unique)
$report = [ordered]@{
    result = $analysis.result
    commit = $commit; apk_sha256 = $apkHash; apk = $apkPath
    serial = $Serial; device = $device; android = $android; build_fingerprint = $build
    vulkan_driver_property = $driver; started_utc = $started.ToString('o'); ended_utc = $ended.ToString('o')
    selected_paths = $analysis.selected_paths; orientations = $analysis.orientations
    first_imported = $analysis.first_imported; last_imported = $analysis.last_imported
    first_converted = $analysis.first_converted; last_converted = $analysis.last_converted
    first_status_epoch = $analysis.first_status_epoch; last_status_epoch = $analysis.last_status_epoch
    largest_status_gap_seconds = $analysis.largest_status_gap_seconds
    checks = $analysis.checks
    ahb_probe_evidence = $probe
    last_gate_status = $analysis.last_gate_status
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
Write-Output "Gate report: $(Join-Path $output 'report.json') ($($report.result))"
if ($report.result -eq 'FAIL') { exit 1 }
