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
& $Adb @target logcat -c
if ($LASTEXITCODE -ne 0) { throw 'ADB logcat clear failed' }
& $Adb @target shell am start -n 'com.DefaultCompany.UnityProject/com.unity3d.player.UnityPlayerActivity' | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Gate activity launch failed' }
$log = Join-Path $output 'logcat.txt'
$capture = Start-Process -FilePath $Adb -ArgumentList (@($target) + @('logcat','-v','epoch','Unity:I','AndroidRuntime:E','*:S')) -WindowStyle Hidden -RedirectStandardOutput $log -PassThru
$started = [DateTime]::UtcNow
Write-Output "Capturing $DurationMinutes minutes from $device ($Serial). Rotate portrait / landscape-left / landscape-right, pause and resume, background and foreground, then tap Restart camera in the gate UI."
try { Start-Sleep -Seconds ($DurationMinutes * 60) }
finally {
    if (!$capture.HasExited) { Stop-Process -Id $capture.Id -Force }
}
$ended = [DateTime]::UtcNow
$lines = @(Get-Content -LiteralPath $log | Where-Object { $_ -match 'HV_GPU_GATE' })
$raw = Get-Content -LiteralPath $log -Raw
$statuses = @($lines | Where-Object { $_ -match ' converted=\d+ ' })
$converted = @($statuses | ForEach-Object { if ($_ -match ' converted=(\d+)') { [long]$Matches[1] } })
$paths = @($statuses | ForEach-Object { if ($_ -match ' path=(\d+)') { [int]$Matches[1] } } | Select-Object -Unique)
$uuidRows = @($statuses | Where-Object { $_ -match 'unityDeviceUUID=([0-9a-f]{32}) ncnnDeviceUUID=\1 unityDriverUUID=([0-9a-f]{32}) ncnnDriverUUID=\2' })
$orientations = @($statuses | ForEach-Object { if ($_ -match ' orientation=([^ ]+)') { $Matches[1] } } | Select-Object -Unique)
$checks = [ordered]@{
    duration_at_least_10_minutes = ($ended - $started).TotalMinutes -ge 9.9
    gpu_conversion_observed = $converted.Count -gt 1 -and ($converted[-1] -gt $converted[0])
    selected_copy_path = $paths.Count -eq 1 -and $paths[0] -in @(1,2)
    exact_nonzero_device_and_driver_uuids = $uuidRows.Count -gt 0 -and ($uuidRows[-1] -notmatch 'UUID=0{32}')
    portrait_and_both_landscapes = @(@('Portrait','LandscapeLeft','LandscapeRight') | Where-Object { $orientations -notcontains $_ }).Count -eq 0
    pause_resume = @($lines | Where-Object { $_ -match 'pause=True' }).Count -gt 0 -and @($lines | Where-Object { $_ -match 'pause=False' }).Count -gt 0
    camera_restart = @($lines | Where-Object { $_ -match 'camera restart requested' }).Count -gt 0
    measured_ahb_description = $raw -match 'candidate=\w+ width=\d+ height=\d+ layers=\d+ format=\d+ usage=\d+ stride=\d+'
    external_format_and_features = $raw -match 'producer vk_format=\d+ external_format=\d+ external_features=\d+' -and
        $raw -match 'consumer vk_format=\d+ external_format=\d+ external_features=\d+'
    external_image_query = $raw -match 'externalMemoryFeatures=\d+ compatibleHandleTypes=\d+ maxExtent='
    consumer_sampled_read_only_import = $raw -match 'consumer\.image_usage' -or $raw -match 'consumer vk_format=[^\r\n]+image_usage=4'
    no_unity_exception = $raw -notmatch 'AndroidRuntime.*FATAL EXCEPTION|Unity.*(NullReferenceException|DllNotFoundException|EntryPointNotFoundException)'
}
$gateSource = Get-Content -LiteralPath (Join-Path $root 'unity/HumanVisionDemo/Assets/HumanVision/Demo/Live/HumanVisionAndroidGpuGate.cs') -Raw
$checks['gate_component_has_no_cpu_readback_api'] = $gateSource -notmatch 'AsyncGPUReadback|GetPixels\s*\(|ReadPixels\s*\('
$probe = @($raw -split "`r?`n" | Where-Object { $_ -match 'candidate=|producer vk_format=|consumer vk_format=|externalMemoryFeatures=|failed:' } | Select-Object -Unique)
$report = [ordered]@{
    result = if (@($checks.Values | Where-Object { $_ -eq $false }).Count -eq 0) { 'PASS_CANDIDATE_REQUIRES_USER_REVIEW' } else { 'FAIL' }
    commit = $commit; apk_sha256 = $apkHash; apk = $apkPath
    serial = $Serial; device = $device; android = $android; build_fingerprint = $build
    vulkan_driver_property = $driver; started_utc = $started.ToString('o'); ended_utc = $ended.ToString('o')
    selected_paths = $paths; orientations = $orientations; first_converted = if ($converted.Count) { $converted[0] } else { $null }
    last_converted = if ($converted.Count) { $converted[-1] } else { $null }
    checks = $checks
    ahb_probe_evidence = $probe
    last_gate_status = if ($statuses.Count) { $statuses[-1] } else { $null }
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
Write-Output "Gate report: $(Join-Path $output 'report.json') ($($report.result))"
if ($report.result -eq 'FAIL') { exit 1 }
