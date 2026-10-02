param([ValidateSet('Capabilities','Color','StartupTimeout','CleanupFault','ViewFailure','Lifecycle')][string]$Gate='Capabilities',[string]$Serial='e7c07019',
      [string]$Output='out/input/task5-device',[string]$Unity='D:/Developer/2021.3.45f1/Editor/Unity.exe',
      [ValidateSet('601','709')][string]$ColorMatrix='601',[ValidateSet('Full','Limited')][string]$ColorRange='Limited',[switch]$CropFixture)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/input_gate_owned_process.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath=[IO.Path]::GetFullPath((Join-Path $repo $Output))
if(-not $outputPath.StartsWith(([IO.Path]::GetFullPath("$repo/out/input")+[IO.Path]::DirectorySeparatorChar),[StringComparison]::OrdinalIgnoreCase)){throw 'Gate output must be within out/input'}
$run=Join-Path $outputPath ([DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ'))
$project=Join-Path $outputPath 'project'
$adb='D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe'
$ffmpeg='C:/ffmpeg/bin/ffmpeg.exe'
$server="$repo/out/tools/mediamtx-v1.12.3/mediamtx.exe"
$fixture="$repo/out/input/task4-round1/asymmetric-video-1-h264.mp4"
foreach($folder in @($run,"$project/Assets/Editor","$project/Assets/Plugins/Android/arm64-v8a","$project/Assets/Resources","$project/Packages","$project/ProjectSettings")){New-Item -ItemType Directory -Force $folder | Out-Null}
$buildIdentity=Get-Content "$repo/out/input-native/android/build-source-identity.json" -Raw | ConvertFrom-Json
if($buildIdentity.native_sha256 -ne (Get-FileHash "$repo/out/input-native/android/libhumanvision_input.so").Hash.ToLowerInvariant()){throw 'Native candidate differs from successful build receipt'}
foreach($source in $buildIdentity.sources.PSObject.Properties){if((Get-FileHash (Join-Path $repo $source.Name)).Hash.ToLowerInvariant() -ne $source.Value){throw "Native source differs from successful build receipt: $($source.Name)"}}
Copy-Item "$repo/out/input-native/android/build-source-identity.json" "$run/build-source-identity.json" -Force
$lock=(& py -3.13 "$PSScriptRoot/rtsp_fixture_manifest.py")|ConvertFrom-Json
foreach($pair in @(@($ffmpeg,$lock.ffmpeg_sha256),@($server,$lock.mediamtx_sha256))){if((Get-FileHash $pair[0]).Hash.ToLowerInvariant() -ne $pair[1]){throw 'Controlled fixture tool qualification failed'}}
if((Get-FileHash $fixture).Hash -ne '926DF205B1A8D3E1CF3E0EBEBBDC4E9232F5A0F8D2EB65C2B918C372F2668CFC'){throw 'Task4 controlled H264 fixture identity mismatch'}
if($Gate -in @('Color','StartupTimeout','CleanupFault','ViewFailure','Lifecycle')){
  $fixtureArgs=@('--output',$run,'--matrix',$ColorMatrix,'--range',$ColorRange)
  if($CropFixture){$fixtureArgs+='--crop'}
  if($Gate -eq 'StartupTimeout'){$fixtureArgs+='--startup-timeout'}
  & py -3.13 "$PSScriptRoot/input_color_fixture.py" @fixtureArgs > "$run/color-fixture-build.stdout.txt"
  if($LASTEXITCODE -ne 0){throw 'Locked source color fixture encoding failed'}
  $fixture="$run/controlled-color-h264.mp4"
  Copy-Item "$run/color-fixture.json" "$project/Assets/Resources/input-color-fixture.json" -Force
}
& $adb -s $Serial get-state > "$run/adb-state.txt"
if($LASTEXITCODE -ne 0){throw 'Authorized Android device unavailable'}
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
"rtsp://127.0.0.1:$port/fixture" | Set-Content "$project/Assets/Resources/input-gate-url.txt" -Encoding utf8
$Gate | Set-Content "$project/Assets/Resources/input-gate-mode.txt" -Encoding utf8
'{"dependencies":{"com.unity.modules.androidjni":"1.0.0","com.unity.modules.jsonserialize":"1.0.0","com.unity.modules.imgui":"1.0.0","com.unity.modules.video":"1.0.0","com.unity.modules.audio":"1.0.0"}}' | Set-Content "$project/Packages/manifest.json" -Encoding utf8
'm_EditorVersion: 2021.3.45f1' | Set-Content "$project/ProjectSettings/ProjectVersion.txt"
Copy-Item "$repo/upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputCapabilityProbe.cs" "$project/Assets/AndroidInputCapabilityProbe.cs" -Force
if($Gate -eq 'Lifecycle'){
  Copy-Item "$repo/upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputLifecycleProbe.cs" "$project/Assets/AndroidInputLifecycleProbe.cs" -Force
  foreach($file in Get-ChildItem "$repo/upm/com.blazetc.humanvision.input/Runtime" -Filter '*.cs') {if($file.Name -ne 'FramePreview.cs'){Copy-Item $file.FullName "$project/Assets/$($file.Name)" -Force}}
  Copy-Item "$repo/upm/com.blazetc.humanvision.input/Runtime/Resources/HumanVisionInputOrientation.shader" "$project/Assets/Resources/HumanVisionInputOrientation.shader" -Force
}
else {
  # Build helper references lifecycle type, compile it in diagnostic APKs too.
  Copy-Item "$repo/upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputLifecycleProbe.cs" "$project/Assets/AndroidInputLifecycleProbe.cs" -Force
  foreach($file in Get-ChildItem "$repo/upm/com.blazetc.humanvision.input/Runtime" -Filter '*.cs') {if($file.Name -ne 'FramePreview.cs'){Copy-Item $file.FullName "$project/Assets/$($file.Name)" -Force}}
}
Copy-Item "$PSScriptRoot/AndroidInputCapabilityBuild.cs" "$project/Assets/Editor/AndroidInputCapabilityBuild.cs" -Force
New-Item -ItemType Directory -Force "$run/source-artifacts" | Out-Null
Copy-Item "$project/Assets/AndroidInputCapabilityProbe.cs","$project/Assets/Editor/AndroidInputCapabilityBuild.cs","$project/Packages/manifest.json",$PSCommandPath,"$PSScriptRoot/check_input_color_gate.py","$PSScriptRoot/input_color_fixture.py" "$run/source-artifacts/" -Force
foreach($file in Get-ChildItem "$project/Assets" -Filter '*.cs'){Copy-Item $file.FullName "$run/source-artifacts/$($file.Name)" -Force}
Copy-Item "$PSScriptRoot/analyze_android_input_gate.py" "$run/source-artifacts/" -Force
Copy-Item "$PSScriptRoot/input_gate_owned_process.ps1" "$run/source-artifacts/" -Force
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" "$project/Assets/Plugins/Android/arm64-v8a/libhumanvision_input.so" -Force
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" "$run/libhumanvision_input.so" -Force
foreach($name in @('avformat','avcodec','avutil','swresample')){Copy-Item "$repo/out/live-deps/ffmpeg-android/lib$name.so" "$project/Assets/Plugins/Android/arm64-v8a/lib$name.so" -Force}
$apk="$run/input-capability.apk"
$owned=@()
function StartOwned($exe,$arguments,$stdout,$stderr) {
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $identity=New-InputGateProcessIdentity $process $exe
  $script:owned+=$identity
  $script:owned | ConvertTo-Json -Depth 4 | Set-Content "$run/owned-processes.json"
  return $process
}
$reversed=$false
try {
  $arguments="-batchmode -nographics -projectPath `"$project`" -executeMethod AndroidInputCapabilityBuild.Build -inputGateApk `"$apk`" -logFile `"$run/unity-build.log`" -quit"
  $info=[Diagnostics.ProcessStartInfo]::new($Unity,$arguments);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WindowStyle='Hidden';$info.Environment['__COMPAT_LAYER']='RunAsInvoker'
  $process=[Diagnostics.Process]::Start($info)
  $owned+=@{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;exe=$process.Path}
  $owned | ConvertTo-Json -Depth 4 | Set-Content "$run/owned-processes.json"
  $process.WaitForExit()
  if($process.ExitCode -ne 0 -or -not(Test-Path $apk)){throw "Input-only Unity APK build failed: $($process.ExitCode)"}
  $audit=@'
import sys,pathlib,zipfile,hashlib,json
repo,run=map(pathlib.Path,sys.argv[1:])
with zipfile.ZipFile(run/'input-capability.apk') as apk:
 entries=[n for n in apk.namelist() if n.startswith('lib/') and n.endswith('.so')]
 if any(('onnxruntime' in n or 'ncnn' in n or n.endswith('/libhumanvision.so') or 'runtime_host' in n) for n in entries): raise RuntimeError('Inference native library in input-only APK')
 receipt={}
 for leaf in ('libhumanvision_input.so','libavformat.so','libavcodec.so','libavutil.so','libswresample.so'):
  entry='lib/arm64-v8a/'+leaf
  data=apk.read(entry); digest=hashlib.sha256(data).hexdigest()
  source=run/leaf if leaf=='libhumanvision_input.so' else repo/'out/live-deps/ffmpeg-android'/leaf
  if hashlib.sha256(source.read_bytes()).hexdigest()!=digest: raise RuntimeError('APK plugin bytes differ: '+leaf)
  receipt[entry]=digest
  if leaf=='libhumanvision_input.so':(run/'apk-libhumanvision_input.so').write_bytes(data)
 (run/'apk-native-entries.json').write_text(json.dumps({'all_native_entries':entries,'qualified_entry_sha256':receipt},indent=2))
'@
  & py -3.13 -c $audit $repo $run
  if($LASTEXITCODE -ne 0){throw 'Input-only APK native identity audit failed'}
  & $adb -s $Serial install -r $apk > "$run/install.log"
  if($LASTEXITCODE -ne 0){throw 'Input-only APK install failed'}
  & $adb -s $Serial shell getprop > "$run/device-properties.txt"
  & $adb -s $Serial shell pm path com.blazetc.humanvision.inputgate > "$run/installed-path.txt"
  $installedPath=((Get-Content "$run/installed-path.txt" | Select-Object -First 1) -replace '^package:','').Trim()
  & $adb -s $Serial pull $installedPath "$run/installed-base.apk" > "$run/installed-pull.log" 2>&1
  if($LASTEXITCODE -ne 0 -or (Get-FileHash "$run/installed-base.apk").Hash -ne (Get-FileHash $apk).Hash){throw 'Installed APK identity mismatch'}
  $config="$run/mediamtx.yml"
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
"@ | Set-Content $config
  $serverProcess=StartOwned $server "`"$config`"" "$run/server.stdout.log" "$run/server.stderr.log"
  Start-Sleep -Milliseconds 800
  $publisher=StartOwned $ffmpeg "-hide_banner -nostdin -stream_loop -1 -re -i `"$fixture`" -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:$port/fixture" "$run/publisher.stdout.log" "$run/publisher.stderr.log"
  Start-Sleep -Seconds 2
  & $adb -s $Serial reverse --no-rebind "tcp:$port" "tcp:$port" > "$run/reverse.log"
  if($LASTEXITCODE -ne 0){throw 'Owned ADB reverse failed'};$reversed=$true
  & $adb -s $Serial shell am force-stop com.blazetc.humanvision.inputgate | Out-Null
  & $adb -s $Serial logcat -c
  $logCollector=StartOwned $adb "-s $Serial logcat -v threadtime" "$run/device-logcat-stream.txt" "$run/device-logcat-stream.stderr.txt"
  & $adb -s $Serial shell monkey -p com.blazetc.humanvision.inputgate 1 > "$run/launch.log"
  if($Gate -eq 'Lifecycle'){
    $timer=[Diagnostics.Stopwatch]::StartNew();$disconnected=$false;$restored=$false;$pausedApp=$false;$resumedApp=$false;$captured=$false;$mutations=@()
    while($timer.Elapsed.TotalSeconds -lt 190){
      Start-Sleep -Seconds 2;$log=Get-Content "$run/device-logcat-stream.txt" -Raw
      if(-not $captured -and $log -match 'gpu_frame_published'){
        & $adb -s $Serial shell screencap -p /sdcard/hv-input-task7-live.png
        & $adb -s $Serial pull /sdcard/hv-input-task7-live.png "$run/live-production-preview.png" > "$run/live-screenshot-pull.log"
        if($LASTEXITCODE -ne 0){throw 'Production live preview screenshot failed'}
        & $adb -s $Serial shell rm /sdcard/hv-input-task7-live.png
        $captured=$true;$mutations+=@{event='actual_live_production_preview_capture';seconds=$timer.Elapsed.TotalSeconds}
      }
      if($log -match 'lifecycle_playback_started' -and $timer.Elapsed.TotalSeconds -gt 25 -and -not $disconnected){
        $identity=$owned | Where-Object {$_.pid -eq $publisher.Id} | Select-Object -Last 1
        Stop-InputGateOwnedProcess $identity | Out-Null
        $disconnected=$true;$disconnectAt=$timer.Elapsed.TotalSeconds;$mutations+=@{event='owned_publisher_disconnected';seconds=$disconnectAt}
      }
      if($disconnected -and -not $restored -and $timer.Elapsed.TotalSeconds -gt ($disconnectAt+5)){
        $publisher=StartOwned $ffmpeg "-hide_banner -nostdin -stream_loop -1 -re -i `"$fixture`" -an -c:v copy -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:$port/fixture" "$run/publisher-restart.stdout.log" "$run/publisher-restart.stderr.log"
        $restored=$true;$mutations+=@{event='owned_publisher_restored';seconds=$timer.Elapsed.TotalSeconds}
      }
      if($restored -and $timer.Elapsed.TotalSeconds -gt 50 -and -not $pausedApp){& $adb -s $Serial shell input keyevent 3 | Out-Null;$pausedApp=$true;$pauseAt=$timer.Elapsed.TotalSeconds;$mutations+=@{event='actual_app_home_pause';seconds=$pauseAt}}
      if($pausedApp -and -not $resumedApp -and $timer.Elapsed.TotalSeconds -gt ($pauseAt+4)){& $adb -s $Serial shell monkey -p com.blazetc.humanvision.inputgate 1 > "$run/resume.log";$resumedApp=$true;$mutations+=@{event='actual_app_resume';seconds=$timer.Elapsed.TotalSeconds}}
      $mutations | ConvertTo-Json -Depth 4 | Set-Content "$run/lifecycle-controlled-events.json"
      if($log -match 'lifecycle_result=PASS|lifecycle_result=FAIL'){break}
    }
  } elseif($Gate -in @('Color','ViewFailure')) {
    Start-Sleep -Seconds 8
    & $adb -s $Serial shell screencap -p /sdcard/hv-input-task6-live.png
    & $adb -s $Serial pull /sdcard/hv-input-task6-live.png "$run/live-gpu-preview.png" > "$run/live-screenshot-pull.log"
    if($LASTEXITCODE -ne 0){throw 'Actual live GPU diagnostic screenshot failed'}
    & $adb -s $Serial shell rm /sdcard/hv-input-task6-live.png
    Start-Sleep -Seconds 18
    & $adb -s $Serial shell screencap -p /sdcard/hv-input-task6-complete.png
    & $adb -s $Serial pull /sdcard/hv-input-task6-complete.png "$run/static-test-snapshot.png" > "$run/static-screenshot-pull.log"
    if($LASTEXITCODE -ne 0){throw 'Actual completed diagnostic screenshot failed'}
    & $adb -s $Serial shell rm /sdcard/hv-input-task6-complete.png
    Start-Sleep -Seconds 19
  } elseif($Gate -eq 'StartupTimeout') {
    $captureTimer=[Diagnostics.Stopwatch]::StartNew();$errorVisible=$false
    while($captureTimer.Elapsed.TotalSeconds -lt 18){
      $phase=& $adb -s $Serial logcat -d -s 'HVInputGate:I' 'Unity:I' '*:S'
      if(($phase -join "`n") -match 'visible_startup_error=true reason=RTSP startup keyframe not received within configured timeout'){$errorVisible=$true;break}
      Start-Sleep -Milliseconds 500
    }
    if(-not $errorVisible){throw 'Native startup ERROR was not reflected by the diagnostic UI before screenshot deadline'}
    Start-Sleep -Milliseconds 500
    @{capturePhase='actual_managed_native_timeout_ERROR';captureUtc=[DateTime]::UtcNow.ToString('o');elapsedAfterLaunchSeconds=$captureTimer.Elapsed.TotalSeconds} | ConvertTo-Json | Set-Content "$run/startup-screenshot-state.json"
    & $adb -s $Serial shell screencap -p /sdcard/hv-input-task6-timeout.png
    & $adb -s $Serial pull /sdcard/hv-input-task6-timeout.png "$run/startup-error.png" > "$run/startup-screenshot-pull.log"
    if($LASTEXITCODE -ne 0){throw 'Actual configured startup ERROR screenshot failed'}
    & $adb -s $Serial shell rm /sdcard/hv-input-task6-timeout.png
    if($captureTimer.Elapsed.TotalSeconds -lt 22){Start-Sleep -Milliseconds ([int]((22-$captureTimer.Elapsed.TotalSeconds)*1000))}
  } else {Start-Sleep -Seconds 22}
  & $adb -s $Serial logcat -d > "$run/device-logcat-final-snapshot.txt"
  $logIdentity=$owned | Where-Object {$_.pid -eq $logCollector.Id} | Select-Object -Last 1
  Stop-InputGateOwnedProcess $logIdentity | Out-Null
  if(-not(Test-Path "$run/device-logcat-stream.txt")){throw 'Owned continuous device log capture missing'}
  Copy-Item "$run/device-logcat-stream.txt" "$run/device-logcat.txt"
  $raw=Get-Content "$run/device-logcat.txt" -Raw
  $decoded=@([regex]::Matches($raw,'decoded codec=[^\r\n]+') | ForEach-Object {$_.Value})
  $returned=@([regex]::Matches($raw,'decoded_lease_returned=true[^\r\n]+active_images=0 active_ahb_references=0 active_owned_fds=0'))
  $pass=$decoded.Count -eq 3 -and $returned.Count -eq 3 -and @($decoded | Where-Object {$_ -match 'logical_proven=1' -and $_ -match 'query=1 failure=0'}).Count -eq 3 -and $raw -notmatch 'capability_result=FAIL' -and $raw -match 'input_only_preinit_hook=1' -and $raw -match 'device_creation success' -and $raw -match 'sync_fd_properties importable=1 exportable=1' -and $raw -match 'decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0' -and $raw -match 'bounded_probe_closed=true'
  if($Gate -eq 'Color'){$pass=$raw -match 'color_pixels=PASS' -and $raw -notmatch 'color_result=FAIL|color_pixels=FAIL|capability_result=FAIL' -and $raw -match 'foreign_extension_successful_device=1' -and $raw -match 'gpu_color_counters.+errors=0 cache_live=0 cpu_image_readbacks=0' -and $raw -match 'decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0'}
  if($Gate -eq 'ViewFailure'){$pass=$raw -match 'target_view_fault_injected=true retired_handle_null=1' -and $raw -match 'target_view_replacement_old_retired=true' -and $raw -match 'color_pixels=PASS' -and $raw -match 'gpu_color_counters.+errors=1 cache_live=0 cpu_image_readbacks=0' -and $raw -match 'bounded_probe_closed=true'}
  if($Gate -in @('Color','ViewFailure')){
    $analysisArgs=@($run);if($Gate -eq 'ViewFailure'){$analysisArgs+='--expected-target-view-failure'}
    & py -3.13 "$PSScriptRoot/check_input_color_gate.py" @analysisArgs > "$run/color-analysis.stdout.txt"
    $pass=$pass -and $LASTEXITCODE -eq 0
  }
  if($Gate -eq 'CleanupFault'){$pass=$raw -match 'cleanup_fault_injected=true actual_gpu_active=1' -and $raw -match 'error_cleanup_complete=true responsive_retirement_frames=[1-9]' -and $raw -match 'gpu_color_completed sequence=1' -and $raw -match 'gpu_color_counters imports=1 destroys=1 submits=1 completes=1' -and $raw -match 'target_views_created=1 target_views_destroyed=1' -and $raw -match 'errors=0 cache_live=0 cpu_image_readbacks=0' -and $raw -notmatch 'Fatal signal' -and $raw -match 'bounded_probe_closed=true' -and $raw -match 'decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0'}
  if($Gate -eq 'StartupTimeout'){
    $pass=$raw -match 'capability_result=FAIL stage=RTSP startup keyframe not received within configured timeout code=-110' -and $raw -notmatch 'decoder_first_keyframe=true|gpu_color_submitted|gpu_color_completed|Fatal signal' -and $raw -match 'decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0' -and $raw -match 'gpu_color_counters imports=0 destroys=0 submits=0 completes=0' -and $raw -match 'errors=0 cache_live=0 cpu_image_readbacks=0' -and $raw -match 'bounded_probe_closed=true' -and $raw -match 'visible_startup_error=true reason=RTSP startup keyframe not received within configured timeout'
    @{status=$(if($pass){'PASS_EXPECTED_TIMEOUT'}else{'FAIL'});expectedNativeError='configured startup keyframe timeout';configuredTimeoutMs=5000;actualGpuSubmissions=0;nativeResourcesBalanced=$pass;fixture=(Get-Content "$run/color-fixture.json" -Raw|ConvertFrom-Json)}|ConvertTo-Json -Depth 8|Set-Content "$run/startup-timeout-analysis.json"
  }
  if($Gate -eq 'Lifecycle'){
    Copy-Item "$PSScriptRoot/analyze_android_input_gate.py" "$run/source-artifacts/analyze_android_input_gate.py" -Force
    & py -3.13 "$run/source-artifacts/analyze_android_input_gate.py" $run > "$run/lifecycle-analysis.stdout.txt"
    $pass=$LASTEXITCODE -eq 0
  }
  $hashes=[ordered]@{}
  if($Gate -eq 'Lifecycle' -and (Test-Path "$run/live-production-preview.png")){$hashes["$run/live-production-preview.png"]=(Get-FileHash "$run/live-production-preview.png").Hash}
  foreach($file in @($apk,"$run/installed-base.apk",$fixture,$server,$ffmpeg,$Unity,$adb,"$run/libhumanvision_input.so")){ $hashes[$file]=(Get-FileHash $file).Hash }
  foreach($file in @(Get-ChildItem "$run/source-artifacts" -File -ErrorAction SilentlyContinue)){ $hashes[$file.FullName]=(Get-FileHash $file.FullName).Hash }
  if($Gate -in @('Color','ViewFailure')){foreach($file in @("$run/live-gpu-preview.png","$run/static-test-snapshot.png")){if(Test-Path $file){$hashes[$file]=(Get-FileHash $file).Hash}}}
  if($Gate -eq 'StartupTimeout'){$hashes["$run/startup-error.png"]=(Get-FileHash "$run/startup-error.png").Hash}
  @{gate=$Gate;serial=$Serial;status=$(if($pass){'PASS'}else{'FAIL'});protocol='RTSP_TCP';codec='H264';port=$port;apk=$apk;project=$project;artifactSha256=$hashes;decodedRecords=$decoded;sourceGeneration='actual native decoded records in device-logcat.txt'} | ConvertTo-Json -Depth 6 | Set-Content "$run/result.json"
  Select-String -Path "$run/device-logcat.txt" -Pattern 'HVInputGate' | ForEach-Object {$_.Line}
  if(-not $pass){throw "Actual Android capability gate FAIL; raw evidence $run"}
} finally {
  & $adb -s $Serial shell am force-stop com.blazetc.humanvision.inputgate | Out-Null
  if($reversed){& $adb -s $Serial reverse --remove "tcp:$port" | Out-Null}
  $cleanupFailures=@()
  foreach($identity in $owned){try {Stop-InputGateOwnedProcess $identity | Out-Null} catch {$cleanupFailures+=$_.Exception.Message}}
  @{reverseRemoved=$reversed;ownedProcesses=$owned;status=$(if($cleanupFailures.Count){'FAIL'}else{'PASS'});errors=$cleanupFailures;closedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content "$run/cleanup.json"
  if($cleanupFailures.Count){throw "Owned process cleanup failed: $($cleanupFailures -join '; ')"}
}
