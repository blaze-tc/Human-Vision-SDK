param(
    [ValidateRange(2,6)][int]$Interval,
    [ValidateRange(1,2)][int]$Capacity,
    [ValidateRange(5,960)][int]$DurationSeconds = 65,
    [string]$Serial = 'e7c07019',
    [string]$Adb = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe',
    [string]$Aapt = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/build-tools/34.0.0/aapt.exe',
    [string]$RunLabel = 'diagnostic',
    [ValidateRange(0,2)][int]$VisiblePersonCount = 0,
    [switch]$SkipInstall,
    [switch]$VideoDiagnostic
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$suffix = if ($VideoDiagnostic) { '-video-diagnostic' } else { '' }
$build = Join-Path $root "out/android-topdown-eval/interval-$Interval-capacity-$Capacity$suffix"
$apk = Join-Path $build 'humanvision-topdown.apk'
$hashesPath = Join-Path $build 'hashes.json'
if (!(Test-Path -LiteralPath $apk) -or !(Test-Path -LiteralPath $hashesPath)) { throw "Verified TopDown APK/hash manifest missing: $build" }
$hashes = Get-Content -LiteralPath $hashesPath -Raw | ConvertFrom-Json
$apkHash = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
if ($apkHash -ne $hashes.apk_sha256 -or $hashes.interval -ne $Interval -or $hashes.capacity -ne $Capacity) { throw 'APK/hash/interval/capacity mismatch' }
if ($VideoDiagnostic) {
    $video = Join-Path $build "UnityProject/Assets/StreamingAssets/HumanVision/Diagnostic/$($hashes.video_name)"
    if (!(Test-Path -LiteralPath $video) -or
        (Get-FileHash -LiteralPath $video -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes.video_sha256) {
        throw 'Video diagnostic source/hash mismatch'
    }
}
$badging = (& $Aapt dump badging $apk | Select-Object -First 1).ToString()
if ($badging -notmatch "^package: name='([^']+)' ") { throw 'Cannot identify APK package' }
$package = $Matches[1]
$target = @('-s', $Serial)
$devices = @(& $Adb devices | Where-Object { $_ -match "^$([regex]::Escape($Serial))\s+device(?:\s|$)" })
if ($devices.Count -ne 1) { throw "Authorized device $Serial is not attached" }
$run = Join-Path $build "device-$RunLabel"
if (Test-Path -LiteralPath $run) { throw "Evidence directory exists; choose a new -RunLabel: $run" }
New-Item -ItemType Directory -Path $run -Force | Out-Null
$started = [DateTime]::UtcNow
$device = (& $Adb @target shell getprop ro.product.model).Trim()
$fingerprint = (& $Adb @target shell getprop ro.build.fingerprint).Trim()
if (!$SkipInstall) {
    & $Adb @target install -r $apk | Out-File -LiteralPath (Join-Path $run 'install.txt') -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw 'ADB install failed' }
}
$installed = @(& $Adb @target shell pm path $package | Where-Object { $_ -match '^package:.+/base\.apk\s*$' } | Select-Object -First 1)
if ($installed.Count -ne 1 -or $installed[0] -notmatch '^package:(.+/base\.apk)\s*$') { throw 'Installed evaluation base APK cannot be identified' }
$installedPath = $Matches[1]
$deviceHashLine = (& $Adb @target shell sha256sum $installedPath).Trim()
if ($deviceHashLine -notmatch '^([0-9a-fA-F]{64})\s+' -or $Matches[1].ToLowerInvariant() -ne $apkHash) {
    throw 'Installed package base APK SHA-256 differs from bound local evaluation APK'
}
& $Adb @target shell am force-stop $package | Out-Null
& $Adb @target logcat -c -b all | Out-Null
& $Adb @target shell am start -n "$package/com.unity3d.player.UnityPlayerActivity" | Out-File -LiteralPath (Join-Path $run 'launch.txt') -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'ADB launch failed' }
Start-Sleep -Seconds 3
$pidText = (& $Adb @target shell pidof $package).Trim()
if ($pidText -notmatch '^\d+$') { throw "Cannot identify launched package PID: $pidText" }
$pidNumber = [int]$pidText
$readyEpoch = $null
$videoActiveLine = $null
$videoFrameLine = $null
$videoFrameFirst = $null
$videoFrameLast = $null
$cameraSourceLine = $null
for ($attempt = 0; $attempt -lt 90; ++$attempt) {
    $snapshot = @(& $Adb @target logcat -d -v epoch "--pid=$pidNumber")
    $cameraSourceLine = $snapshot | Where-Object { $_ -match 'HV_TOPDOWN_CAMERA device=' } | Select-Object -First 1
    if ($VideoDiagnostic) {
        $videoActiveLine = $snapshot | Where-Object { $_ -match "HV_TOPDOWN_VIDEO_SOURCE_ACTIVE name=$([regex]::Escape($hashes.video_name)) sha256=$($hashes.video_sha256)" } | Select-Object -First 1
        $videoFrames = @($snapshot | Where-Object { $_ -match 'HV_TOPDOWN_VIDEO_FRAME .*submitted=True' })
        if ($videoFrames.Count -ge 2 -and
            $videoFrames[0] -match 'index=(\d+)' -and $videoFrames[-1] -match 'index=(\d+)') {
            $videoFrameFirst = [long]([regex]::Match($videoFrames[0], 'index=(\d+)').Groups[1].Value)
            $videoFrameLast = [long]([regex]::Match($videoFrames[-1], 'index=(\d+)').Groups[1].Value)
            if ($videoFrameLast -gt $videoFrameFirst) { $videoFrameLine = $videoFrames[-1] }
        }
        if ($cameraSourceLine) {
            throw 'Video diagnostic opened WebCamera; source identity is invalid'
        }
    } elseif ($snapshot | Where-Object { $_ -match 'HV_TOPDOWN_VIDEO_SOURCE_ACTIVE ' } | Select-Object -First 1) {
        throw 'Live camera gate activated video diagnostic source'
    }
    $readyLine = $snapshot | Where-Object { $_ -match 'HV_TOPDOWN_STATS .*source_seen=[1-9][0-9]* .*submitted=[1-9][0-9]*' } | Select-Object -First 1
    if ($readyLine -and (($VideoDiagnostic -and $videoActiveLine -and $videoFrameLine) -or
            (!$VideoDiagnostic -and $cameraSourceLine)) -and $readyLine -match '^\s*(\d+\.\d+)') {
        $readyEpoch = [double]$Matches[1]; break
    }
    Start-Sleep -Seconds 1
    $livePid = (& $Adb @target shell pidof $package).Trim()
    if ($livePid -ne $pidText) { throw "Evaluation package PID changed before stream readiness: $livePid" }
}
if ($null -eq $readyEpoch) { throw 'No real source frame was submitted within 90 seconds' }
for ($remaining = $DurationSeconds; $remaining -gt 0; $remaining -= 10) {
    Start-Sleep -Seconds ([Math]::Min(10, $remaining))
    $livePid = (& $Adb @target shell pidof $package).Trim()
    if ($livePid -ne $pidText) { throw "Evaluation package PID changed during collection: $livePid" }
}
$raw = Join-Path $run 'logcat.txt'
& $Adb @target logcat -d -v epoch "--pid=$pidNumber" | Set-Content -LiteralPath $raw -Encoding utf8
if ($LASTEXITCODE -ne 0) { throw 'ADB log capture failed' }
if ($VideoDiagnostic -and (Select-String -LiteralPath $raw -Pattern 'HV_TOPDOWN_CAMERA device=' -Quiet)) {
    throw 'Video diagnostic opened WebCamera during collection; source identity is invalid'
}
$ended = [DateTime]::UtcNow
$actualPackage = (& $Adb @target shell pidof $package).Trim()
$report = [ordered]@{
    result = 'FAIL'
    reason = 'SENSOR_VERIFIED capture provenance and user physical correctness evidence are required; automated run is diagnostic'
    video_diagnostic = [bool]$VideoDiagnostic
    video_active_line = $videoActiveLine
    video_frame_line = $videoFrameLine
    video_frame_index_first = $videoFrameFirst
    video_frame_index_last = $videoFrameLast
    camera_source_line = $cameraSourceLine
    interval = $Interval; capacity = $Capacity; package = $package; pid = $pidNumber
    serial = $Serial; device = $device; fingerprint = $fingerprint
    apk_sha256 = $apkHash; hashes = $hashes
    install_skipped = [bool]$SkipInstall
    install_identity_verified = $true
    installed_apk_sha256 = $apkHash
    stream_ready_log_epoch = $readyEpoch
    started_utc = $started.ToString('o'); ended_utc = $ended.ToString('o')
    intended_duration_seconds = $DurationSeconds
    annotated_visible_person_count = $(if ($VisiblePersonCount -gt 0) { $VisiblePersonCount } else { $null })
    same_pid_at_end = $actualPackage -eq $pidText
    logcat_sha256 = (Get-FileHash -LiteralPath $raw -Algorithm SHA256).Hash.ToLowerInvariant()
    stats_lines = @((Get-Content -LiteralPath $raw) | Where-Object { $_ -match 'HV_TOPDOWN_STATS' })
    sampled_observation_lines = @((Get-Content -LiteralPath $raw) | Where-Object { $_ -match 'HV_TOPDOWN_SAMPLE' })
    fatal_lines = @((Get-Content -LiteralPath $raw) | Where-Object { $_ -match 'AndroidRuntime|Fatal signal|ANR|Exception|Error' } | Select-Object -First 100)
    log_lines = @(Get-Content -LiteralPath $raw)
}
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
& (Join-Path $root '.venv-reference/Scripts/python.exe') -m tools.test.summarize_android_topdown_gate (Join-Path $run 'report.json')
if ($LASTEXITCODE -ne 0) { throw 'Gate summary/analyzer failed' }
Write-Output "Diagnostic TopDown report: $(Join-Path $run 'report.json')"
