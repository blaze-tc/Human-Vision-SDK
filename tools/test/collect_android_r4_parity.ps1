param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9-]+$')][string]$RunLabel,
    [string]$Serial = 'e7c07019',
    [int]$CaseIndex = -1,
    [ValidateRange(0,2)][int]$CopyPath = 0,
    [switch]$ReuseInstalledApk,
    [switch]$RequireTicketMetadata,
    [string]$NativeLibrary = 'build/android-live/bin/Release/libhumanvision.so',
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$Adb = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe',
    [string]$Aapt = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/build-tools/34.0.0/aapt.exe'
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
$build=(Resolve-Path -LiteralPath $BuildDirectory).Path
$manifestPath=(Resolve-Path -LiteralPath $Manifest).Path
$manifestHash=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
$document=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
$fixture=$document
if($document.cases) {
    if($CaseIndex -lt 0 -or $CaseIndex -ge $document.cases.Count) { throw 'Analytic manifest requires a valid CaseIndex' }
    $fixture=$document.cases[$CaseIndex]
} elseif($CaseIndex -ne -1) { throw 'CaseIndex is only valid for an analytic manifest' }
$apk=Join-Path $build 'humanvision-topdown.apk'
$apkHash=(Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
# Use the same verified APK dependency-closure gate as the integrated builder.
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify_android_gpu_bridge_gate_libs.ps1') -Mode Verify -ApkPath $apk -NativeLibrary $NativeLibrary
if($LASTEXITCODE -ne 0) { throw 'APK library closure failed' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($apk)
try {
    $entry=$zip.GetEntry('assets/HumanVision/R4/manifest.json')
    if(!$entry) { throw 'R4 manifest missing from actual APK' }
    $stream=$entry.Open(); $sha=[Security.Cryptography.SHA256]::Create()
    try { $embedded=[Convert]::ToHexString($sha.ComputeHash($stream)).ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
    if($embedded -ne $manifestHash) { throw 'APK embedded manifest does not match requested fixture' }
} finally { $zip.Dispose() }
$badging=(& $Aapt dump badging $apk | Select-Object -First 1).ToString()
if($badging -notmatch "^package: name='([^']+)' ") { throw 'Cannot identify APK package' }
$package=$Matches[1]
if(!$package.EndsWith('.parity')) { throw 'R4 collector requires the static-parity application identity' }
$target=@('-s',$Serial)
if((& $Adb @target get-state).Trim() -ne 'device') { throw 'Authorized device unavailable' }
$run=Join-Path $root "out/android-r4/device-$RunLabel"
if(Test-Path -LiteralPath $run) { throw 'RunLabel must identify a new evidence directory' }
New-Item -ItemType Directory -Path $run | Out-Null
$report=[ordered]@{task2_pass=$false; serial=$Serial; package=$package; apk_sha256=$apkHash; manifest_sha256=$manifestHash;
    case_index=$CaseIndex; requested_copy_path=$CopyPath; native_sha256=(Get-FileHash -LiteralPath $NativeLibrary).Hash.ToLowerInvariant();
    fingerprint=((& $Adb @target shell getprop ro.build.fingerprint).Trim()); started_utc=[DateTime]::UtcNow.ToString('o');
    identity_verified=$false; source_active=$false; completed=$false; stages=@(); error=$null}
try {
    if(!$ReuseInstalledApk) {
        & $Adb @target install -r $apk | Set-Content -LiteralPath (Join-Path $run 'install.txt')
        if($LASTEXITCODE -ne 0) { throw 'ADB install failed' }
    }
    $installed=@(& $Adb @target shell pm path $package | Where-Object { $_ -match '^package:.+/base\.apk\s*$' })
    if($installed.Count -ne 1 -or $installed[0] -notmatch '^package:(.+/base\.apk)\s*$') { throw 'Installed base APK not uniquely identified' }
    $deviceHash=(& $Adb @target shell sha256sum $Matches[1]).Trim()
    if($deviceHash -notmatch '^([0-9a-fA-F]{64})\s+' -or $Matches[1].ToLowerInvariant() -ne $apkHash) { throw 'Installed APK hash differs' }
    $report.identity_verified=$true
    & $Adb @target shell am force-stop $package | Out-Null
    & $Adb @target logcat -c -b all | Out-Null
    # Start before launch/PID discovery so initial native identity and probes
    # survive ring-buffer rollover. Keep duplicate records; timestamps/PIDs
    # distinguish events. A separate process continuously drains logcat.
    $streamLog=Join-Path $run 'logcat-stream.txt'
    $logProcess=Start-Process -FilePath $Adb -ArgumentList @('-s',$Serial,'logcat','-v','epoch') -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $streamLog -RedirectStandardError (Join-Path $run 'logcat-stream-errors.txt')
    & $Adb @target shell input keyevent KEYCODE_WAKEUP | Out-Null
    & $Adb @target shell am start -n "$package/com.unity3d.player.UnityPlayerActivity" --ei r4_case $CaseIndex --ei r4_copy_path $CopyPath | Set-Content -LiteralPath (Join-Path $run 'launch.txt')
    if($LASTEXITCODE -ne 0) { throw 'ADB launch failed' }
    Start-Sleep -Seconds 3
    $appPid=(& $Adb @target shell pidof $package).Trim()
    if($appPid -notmatch '^\d+$') { throw 'Launched PID not uniquely identified' }
    $report.pid=[int]$appPid
    for($attempt=0;$attempt -lt 90;$attempt++) {
        $lines=@(Get-Content -LiteralPath $streamLog | Where-Object { $_ -match "^\s*\d+\.\d+\s+$appPid\s+" })
        $lines | Set-Content -LiteralPath (Join-Path $run 'logcat.txt') -Encoding utf8
        if($lines | Where-Object { $_ -match 'HV_TOPDOWN_CAMERA device=|HV_TOPDOWN_VIDEO_SOURCE_ACTIVE' }) { throw 'Unexpected camera/video source activated' }
        if($lines | Where-Object { $_ -match "HV_R4_SOURCE manifest_sha256=$manifestHash .*route=static_gpu_upload" -and
            ($CaseIndex -lt 0 -or $_ -match " case=$CaseIndex ") }) { $report.source_active=$true }
        if($lines | Where-Object { $_ -match 'HV_R4_STATIC_DONE' }) { $report.completed=$true; break }
        if($lines | Where-Object { $_ -match 'Fatal signal|FATAL EXCEPTION|EntryPointNotFoundException|InvalidOperationException|InvalidDataException' }) { throw 'Application error; inspect saved logcat' }
        if((& $Adb @target shell pidof $package).Trim() -ne $appPid) { throw 'Application PID changed' }
        Start-Sleep -Seconds 1
    }
    if(!$logProcess.HasExited) { Stop-Process -Id $logProcess.Id; $logProcess.WaitForExit() }
    $lines=@(Get-Content -LiteralPath $streamLog | Where-Object { $_ -match "^\s*\d+\.\d+\s+$appPid\s+" })
    $lines | Set-Content -LiteralPath (Join-Path $run 'logcat.txt') -Encoding utf8
    $foreground=@(& $Adb @target shell dumpsys activity activities | Where-Object { $_ -match 'mResumedActivity|topResumedActivity' })
    $report.foreground=$foreground
    if(!($foreground -match [regex]::Escape($package))) { throw 'Evaluation application is not resumed in foreground' }
    if(!$report.source_active -or !$report.completed) { throw 'Hash-bound static source did not complete' }
    $stages=@()
    foreach($line in $lines) {
        if($line -match 'HV_R4_PARITY\s*:\s*(\{.*\})') {
            $row=$Matches[1] | ConvertFrom-Json
            if($row.manifest_sha256 -ne $manifestHash -or $row.elements -le 0) { throw 'Stage identity/count mismatch' }
            $expectedElements=switch($row.stage) {
                'source' { $fixture.width*$fixture.height*4 }
                'producer' { $fixture.width*$fixture.height*4 }
                'imported_rgb' { $fixture.width*$fixture.height*3 }
                'normalized' { 307200 }
                'packed' { 307200 }
                default { throw 'Unknown parity stage' }
            }
            if($row.elements -ne $expectedElements) { throw 'Stage logical extent differs from selected fixture' }
            if($RequireTicketMetadata) {
                $goldenName=switch($row.stage) { 'normalized' {'tensor_fp32'} 'packed' {'tensor_fp16_rtz'} default {'rgba'} }
                $channels=if($row.stage -in @('source','producer')){4}else{3}
                $width=if($row.stage -in @('normalized','packed')){320}else{$fixture.width}
                $height=if($row.stage -in @('normalized','packed')){320}else{$fixture.height}
                $dtype=switch($row.stage) { 'source' {0} 'producer' {0} 'packed' {1} default {2} }
                if($row.golden_sha256 -ne $fixture.artifacts.$goldenName.sha256 -or
                    $row.device_uuid -notmatch '^[0-9a-f]{32}$' -or $row.device_uuid -eq ('0'*32) -or !$row.vkdevice -or
                    $row.width -ne $width -or $row.height -ne $height -or $row.channels -ne $channels -or
                    $row.dtype -ne $dtype -or $row.elempack -ne 1 -or !$row.row_stride -or !$row.channel_stride -or
                    $row.samples.Count -ne 9 -or $row.first_xyz.Count -ne 3) { throw 'GPU ticket metadata contract mismatch' }
                if(!$row.passed -and $row.mismatches -eq 0) { throw 'GPU ticket rejected a numerically clean summary' }
            }
            $stages+=$row
        }
    }
    $report.stages=$stages
    $configurationPath=Join-Path $build 'configuration.json'
    if(Test-Path -LiteralPath $configurationPath) {
        $configuration=Get-Content -LiteralPath $configurationPath -Raw|ConvertFrom-Json
        $capacities=@($lines|ForEach-Object{if($_ -match 'HV_TOPDOWN_STATS .* capacity=(\d+) '){[int]$Matches[1]}}|Sort-Object -Unique)
        if($capacities.Count -ne 1 -or $capacities[0] -ne $configuration.effective_capacity -or
            $configuration.effective_capacity -lt @($fixture.annotations).Where({$null -ne $_}).Count){throw 'Actual capacity differs from tracked builder configuration/annotations'}
        $report.effective_capacity=$configuration.effective_capacity
        $report.configuration_sha256=(Get-FileHash -LiteralPath $configurationPath).Hash.ToLowerInvariant()
    }
    $report.same_frame_groups=@($stages | Group-Object generation,source_id,slot | Where-Object {
        @($_.Group.stage|Sort-Object -Unique).Count -eq 5
    } | ForEach-Object { $_.Name })
    if(!$report.same_frame_groups.Count) { throw 'No complete five-stage comparison from the same generation/frame/slot' }
    if($RequireTicketMetadata) {
        $uuids=@($stages.device_uuid|Sort-Object -Unique)
        $imageDevices=@($stages|Where-Object{$_.stage -in @('source','producer')}|ForEach-Object{$_.vkdevice}|Sort-Object -Unique)
        $tensorDevices=@($stages|Where-Object{$_.stage -notin @('source','producer')}|ForEach-Object{$_.vkdevice}|Sort-Object -Unique)
        if($uuids.Count -ne 1 -or $imageDevices.Count -ne 1 -or $tensorDevices.Count -ne 1 -or $imageDevices[0] -eq $tensorDevices[0]) {
            throw 'Expected one physical device and two distinct logical devices'
        }
        $report.gpu_fault_controls=@($lines|Where-Object{$_ -match 'gpu_fault_control name=.+ pass=1 '})
        $report.gpu_resource_metadata_controls=@($lines|Where-Object{$_ -match 'gpu_resource_metadata_control stage=.+ pass=1 '})
        if($report.gpu_fault_controls.Count -ne 16 -or $report.gpu_resource_metadata_controls.Count -ne 3) {
            throw 'Required actual GPU fault/metadata controls missing'
        }
        $boundaryReport=Join-Path $run 'boundary-controls.json'
        & (Join-Path $root '.venv-reference/Scripts/python.exe') (Join-Path $PSScriptRoot 'verify_r4_boundary_controls.py') `
            --log (Join-Path $run 'logcat.txt') --output $boundaryReport
        if($LASTEXITCODE -ne 0){throw 'Actual image/import fault controls or clean preceding boundaries failed'}
        $report.gpu_boundary_controls=(Get-Content -LiteralPath $boundaryReport -Raw|ConvertFrom-Json).controls
    }
    $report.measured_copy_paths=@($lines | ForEach-Object {
        if($_ -match 'HV_TOPDOWN_STATS .* copy_path=([12]) ') { [int]$Matches[1] }
    } | Sort-Object -Unique)
    if($CopyPath -ne 0 -and ($report.measured_copy_paths.Count -ne 1 -or $report.measured_copy_paths[0] -ne $CopyPath)) {
        throw 'Measured copy path differs from requested evaluation path'
    }
    $report.detector_outputs=@()
    foreach($line in $lines) {
        if($line -match 'detector_output source=(\d+) name=(cls|bbox) elements=(\d+) saved=1 file=(detector-\d+-(?:cls|bbox)\.f32) sha256=([0-9a-f]{64})') {
            $sourceId=$Matches[1]; $name=$Matches[2]; $elements=[int]$Matches[3]; $filename=$Matches[4]; $expectedHash=$Matches[5]
            $destination=Join-Path $run $filename
            & $Adb @target pull "/sdcard/Android/data/$package/files/r4-parity/$filename" $destination 2>&1 | Out-Null
            if($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $destination).Length -ne $elements*4 -or
               (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash) {
                throw 'Normal detector output artifact verification failed'
            }
            $report.detector_outputs+=@{source_id=$sourceId;name=$name;elements=$elements;file=$filename;sha256=$expectedHash}
        }
    }
    $report.numeric_failures=@($stages | Where-Object {
        $limit=if($_.stage -in @('source','producer','imported_rgb')) { 1.0 } else { 0.02 }
        $_.max -gt $limit -or $_.mismatches -gt 0 -or
            ($_.stage -in @('normalized','packed') -and $_.mean -gt 0.002)
    } | Select-Object stage,source_id,slot,max,mean,mismatches,first)
    $report.input_parity_pass=$false
    $report.native_errors=@($lines | Where-Object { $_ -match 'GPU worker error=.+|Fatal signal|FATAL EXCEPTION|reduction_ticket_error|gpu_fault_control .*pass=0|gpu_resource_metadata_control .*pass=0' })
    $report.reason='Diagnostic capture only; full transform/path/corruption and detector/pose gates remain required'
    if($stages.Count -eq 0) { throw 'No GPU stage summaries were captured' }
    $report.missing_stages=@(@('source','producer','imported_rgb','normalized','packed') | Where-Object { $_ -notin $stages.stage })
    if($report.missing_stages.Count -gt 0) { throw "Required GPU stages missing: $($report.missing_stages -join ', ')" }
    if($report.native_errors.Count -gt 0) { throw 'Native worker errors invalidate the stage run; inspect native_errors' }
    if($report.numeric_failures.Count -gt 0) { throw 'GPU input numerical parity failed; inspect numeric_failures' }
    $report.input_parity_pass=$true
} catch { $report.error=$_.Exception.Message }
finally {
    if($logProcess -and !$logProcess.HasExited) { Stop-Process -Id $logProcess.Id; $logProcess.WaitForExit() }
    $report.ended_utc=[DateTime]::UtcNow.ToString('o')
    if(Test-Path -LiteralPath (Join-Path $run 'logcat.txt')) {
        $report.logcat_sha256=(Get-FileHash -LiteralPath (Join-Path $run 'logcat.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
}
Write-Output "R4 diagnostic evidence: $run"
if($report.error) { throw $report.error }
