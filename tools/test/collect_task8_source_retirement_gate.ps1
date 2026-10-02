param([Parameter(Mandatory=$true)][string]$SourceRoot,[string]$Serial='e7c07019',
      [string]$Output='out/input/task8-device',[string]$ExistingBuild='',[string]$Unity='D:/Developer/2021.3.45f1/Editor/Unity.exe')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/input_gate_owned_process.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=[IO.Path]::GetFullPath((Join-Path $repo $Output))
if(-not $out.StartsWith("$repo\out\input\",[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be inside out/input'}
$run=Join-Path $out ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ'))
New-Item -ItemType Directory -Force $run|Out-Null
$adb='D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe'
$ffmpeg='C:/ffmpeg/bin/ffmpeg.exe';$server="$repo/out/tools/mediamtx-v1.12.3/mediamtx.exe"
$fixture="$repo/out/input/task4-round1/asymmetric-video-1-h264.mp4"
$lock=(& py -3.13 "$PSScriptRoot/rtsp_fixture_manifest.py")|ConvertFrom-Json
foreach($pair in @(@($ffmpeg,$lock.ffmpeg_sha256),@($server,$lock.mediamtx_sha256))){if((Get-FileHash $pair[0]).Hash.ToLowerInvariant() -ne $pair[1]){throw 'Controlled RTSP tool hash changed'}}
if((Get-FileHash $fixture).Hash -ne '926DF205B1A8D3E1CF3E0EBEBBDC4E9232F5A0F8D2EB65C2B918C372F2668CFC'){throw 'Controlled H264 fixture changed'}
& $adb -s $Serial get-state > "$run/device-state.txt";if($LASTEXITCODE -ne 0){throw 'Authorized device unavailable'}
if($ExistingBuild) {
    $build=(Resolve-Path -LiteralPath $ExistingBuild).Path
    if(-not $build.StartsWith("$repo\out\input\",[StringComparison]::OrdinalIgnoreCase)){throw 'Qualified build must be inside out/input'}
    $receipt=Get-Content "$build/build-receipt.json" -Raw|ConvertFrom-Json
    $identity=Get-Content "$build/source-artifact-identity.json" -Raw|ConvertFrom-Json
    $source=(Resolve-Path -LiteralPath $SourceRoot).Path
    if($source -ne $receipt.source_root -or $source -ne $identity.source_root){throw 'Replay source root mismatch'}
    foreach($entry in $identity.sources.PSObject.Properties){if((Get-FileHash (Join-Path $source $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value){throw "Replay source hash changed: $($entry.Name)"}}
    foreach($entry in $identity.artifacts.PSObject.Properties){if((Get-FileHash (Join-Path $build $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value){throw "Replay artifact hash changed: $($entry.Name)"}}
    if($receipt.apk -ne (Get-FileHash "$build/task8-source-retirement.apk").Hash.ToLowerInvariant() -or $receipt.sdk_native -ne (Get-FileHash "$build/native-gate/bin/Release/libhumanvision.so").Hash.ToLowerInvariant()){throw 'Replay native/APK receipt mismatch'}
    $url=(Get-Content "$build/project/Assets/Resources/input-gate-url.txt" -Raw).Trim()
    $uri=[Uri]$url
    if($uri.Scheme -ne 'rtsp' -or $uri.Host -ne '127.0.0.1' -or $uri.AbsolutePath -ne '/fixture' -or $uri.UserInfo){throw 'Replay baked URL is not the controlled loopback fixture'}
    $port=$uri.Port
    Copy-Item "$build/source-artifact-identity.json" "$run/replay-source-artifact-identity.json"
    $apk="$build/task8-source-retirement.apk"
} else {
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
    $url="rtsp://127.0.0.1:$port/fixture"
    & pwsh -NoProfile -File "$PSScriptRoot/build_task8_source_retirement_gate.ps1" -SourceRoot $SourceRoot -Url $url -Output $Output -Unity $Unity *> "$run/build.stdout.log"
    if($LASTEXITCODE -ne 0){throw "Task8 build failed: $run/build.stdout.log"}
    $build=$out;$apk="$out/task8-source-retirement.apk"
}
$verifyUrl='import sys,zipfile; url=sys.argv[2].encode(); archive=zipfile.ZipFile(sys.argv[1]); matches=[n for n in archive.namelist() if n.startswith("assets/") and url in archive.read(n)]; print("actual_APK_baked_URL_assets="+str(matches)); sys.exit(0 if matches else 1)'
& py -3.13 -c $verifyUrl $apk $url *> "$run/baked-url-audit.log"
if($LASTEXITCODE -ne 0){throw 'Actual APK baked controlled URL does not match'}
Copy-Item "$build/build-receipt.json" "$run/build-receipt.json"
$owned=@();$reverse=$false
try {
    & $adb -s $Serial install -r $apk > "$run/install.log";if($LASTEXITCODE -ne 0){throw 'Task8 APK install failed'}
    $remote=(& $adb -s $Serial shell pm path com.blazetc.humanvision.task8gate | Select-Object -First 1) -replace '^package:',''
    & $adb -s $Serial pull $remote.Trim() "$run/installed-base.apk" > "$run/installed-pull.log" 2>&1
    if($LASTEXITCODE -ne 0 -or (Get-FileHash "$run/installed-base.apk").Hash -ne (Get-FileHash $apk).Hash){throw 'Installed Task8 APK hash mismatch'}
    & $adb -s $Serial logcat -c
    & $adb -s $Serial reverse "tcp:$port" "tcp:$port";if($LASTEXITCODE -ne 0){throw 'Owned USB reverse failed'};$reverse=$true
    @"
logLevel: info
rtspAddress: 127.0.0.1:$port
rtspTransports: [tcp]
rtmp: no
hls: no
webrtc: no
srt: no
paths:
  fixture:
    source: publisher
"@|Set-Content "$run/mediamtx.yml"
    $process=Start-Process $server -ArgumentList "`"$run/mediamtx.yml`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.stdout.log" -RedirectStandardError "$run/server.stderr.log"
    $owned+=New-InputGateProcessIdentity $process $server
    Start-Sleep -Milliseconds 800
    $process=Start-Process $ffmpeg -ArgumentList "-hide_banner -nostdin -stream_loop -1 -re -i `"$fixture`" -an -c:v copy -f rtsp -rtsp_transport tcp $url" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/publisher.stdout.log" -RedirectStandardError "$run/publisher.stderr.log"
    $owned+=New-InputGateProcessIdentity $process $ffmpeg
    Start-Sleep -Seconds 2
    & $adb -s $Serial shell am force-stop com.blazetc.humanvision.task8gate
    & $adb -s $Serial shell monkey -p com.blazetc.humanvision.task8gate 1 > "$run/launch.log"
    $timer=[Diagnostics.Stopwatch]::StartNew();$previewCaptured=$false;$closedCaptured=$false
    do {
        Start-Sleep -Milliseconds 500
        & $adb -s $Serial logcat -d > "$run/logcat.txt"
        $raw=Get-Content "$run/logcat.txt" -Raw
        if(-not $previewCaptured -and $raw -match 'HVTask8 preview_ready'){
            & $adb -s $Serial shell screencap -p /sdcard/task8-preview.png
            & $adb -s $Serial pull /sdcard/task8-preview.png "$run/live-preview.png" > "$run/preview-pull.log" 2>&1
            $previewCaptured=$true
        }
        if(-not $closedCaptured -and $raw -match 'HVTask8 close_complete'){
            & $adb -s $Serial shell screencap -p /sdcard/task8-closed.png
            & $adb -s $Serial pull /sdcard/task8-closed.png "$run/closed-with-consumer-held.png" > "$run/closed-pull.log" 2>&1
            $closedCaptured=$true
        }
    } while($timer.Elapsed.TotalSeconds -lt 90 -and $raw -notmatch 'HVTask8 result=(PASS|FAIL)')
    & py -3.13 "$repo/tools/test/analyze_task8_source_retirement_gate.py" $run *> "$run/resource-analysis.stdout.log"
    $resourcePass=$LASTEXITCODE -eq 0
    $pass=$resourcePass -and $previewCaptured -and $closedCaptured -and
          (Test-Path "$run/live-preview.png") -and (Test-Path "$run/closed-with-consumer-held.png")
    if(-not $pass){throw "Task8 physical gate failed: $run/logcat.txt"}
} finally {
    & $adb -s $Serial shell am force-stop com.blazetc.humanvision.task8gate > "$run/force-stop.log" 2>&1
    $cleanup=@();$cleanupErrors=@()
    foreach($identity in $owned){try{$cleanup+=Stop-InputGateOwnedProcess $identity}catch{$cleanupErrors+=$_.Exception.Message}}
    if($reverse){& $adb -s $Serial reverse --remove "tcp:$port" > "$run/reverse-remove.log" 2>&1;if($LASTEXITCODE -ne 0){$cleanupErrors+='Owned Task8 reverse cleanup failed'}}
    $cleanup|ConvertTo-Json|Set-Content "$run/owned-process-cleanup.json"
    if($cleanupErrors.Count){$cleanupErrors|ConvertTo-Json|Set-Content "$run/cleanup-errors.json";throw ($cleanupErrors -join '; ')}
}

# Publisher log handles have closed before immutable artifact hashing.
$hashes=[ordered]@{}
foreach($file in @(Get-ChildItem $run -File)){ $hashes[$file.Name]=(Get-FileHash $file.FullName).Hash.ToLowerInvariant() }
@{status=$(if($pass){'PASS'}else{'FAIL'});boundary='actual SDK source GPU copy + real held ConsumerFrame, no ncnn inference';preview_captured=$previewCaptured;hashes=$hashes}|ConvertTo-Json -Depth 6|Set-Content "$run/analysis.json"
Write-Output "Task8 physical gate PASS: $run"
