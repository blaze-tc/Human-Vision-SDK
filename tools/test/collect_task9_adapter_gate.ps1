param([Parameter(Mandatory=$true)][string]$SourceRoot,[string]$Kind='canonical',[string]$Output='out/input/task9-device',[string]$Serial='e7c07019',[switch]$BuildOnly,[string]$ExistingBuild='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/input_gate_owned_process.ps1"
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'));$source=(Resolve-Path $SourceRoot).Path;$out=Join-Path $repo $Output
if($ExistingBuild){$out=(Resolve-Path $ExistingBuild).Path}else{if(Test-Path $out){throw 'Gate fixture is immutable; choose a new output'}}
$project=Join-Path $out 'project';$native=Join-Path $out 'native';$plugins="$project/Assets/Plugins/Android/arm64-v8a"
if(!$ExistingBuild){
foreach($d in @($out,"$project/Assets/HumanVision/Runtime","$project/Assets/HumanVision/Demo","$project/Assets/Editor","$project/Assets/Resources",$plugins,"$project/Assets/StreamingAssets/HumanVision/Runtime/modelpacks","$project/Assets/StreamingAssets/HumanVision/Runtime/profiles","$project/Packages","$project/ProjectSettings")){New-Item -ItemType Directory -Force $d|Out-Null}
$cmake='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe';$ninja='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe';$ndk='D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK'
if((Get-Content "$ndk/source.properties" -Raw)-notmatch 'Pkg.Revision\s*=\s*23\.1\.7779620'){throw 'Qualified NDK pin mismatch'}
& $cmake -S $source -B $native -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHV_ENABLE_RTSP=OFF -DHV_ANDROID_GPU_GATE=ON "-DHV_NCNN_ROOT=$repo/out/ncnn-20260526/android-arm64-api26/install" "-DHV_ONNXRUNTIME_ROOT=$repo/out/live-deps/ort-android" *> "$out/native-configure.log"
if($LASTEXITCODE -ne 0){throw 'Native configure failed'}
& $cmake --build $native --target humanvision *> "$out/native-build.log";if($LASTEXITCODE -ne 0){throw 'Native build failed'}
& pwsh -NoProfile -File "$repo/tools/test/verify_android_gpu_bridge_gate_libs.ps1" -Mode Stage -NativeLibrary "$native/bin/Release/libhumanvision.so" -PluginDirectory $plugins *> "$out/elf-audit.log";if($LASTEXITCODE -ne 0){throw 'API26 ELF closure failed'}
$inputReceipt=Get-Content "$repo/out/input-native/android/build-source-identity.json" -Raw|ConvertFrom-Json
foreach($entry in $inputReceipt.sources.PSObject.Properties){if((Get-FileHash (Join-Path $repo $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value){throw "Input native source drift: $($entry.Name)"}}
if((Get-FileHash "$repo/out/input-native/android/libhumanvision_input.so").Hash.ToLowerInvariant() -ne $inputReceipt.native_sha256){throw 'Input native artifact drift'}
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" $plugins
foreach($lib in Get-ChildItem "$repo/out/live-deps/ffmpeg-android" -Filter '*.so'){Copy-Item $lib.FullName $plugins}
$runtime=if($Kind -eq 'canonical'){"$source/unity/HumanVisionDemo/Assets/HumanVision/Runtime"}else{"$source/upm/com.blazetc.humanvision/Runtime"}
foreach($f in Get-ChildItem $runtime -Recurse -File){if($f.FullName -match '[\\/]Plugins[\\/]'){continue};$rel=$f.FullName.Substring($runtime.Length+1);$dest=Join-Path "$project/Assets/HumanVision/Runtime" $rel;New-Item -ItemType Directory -Force (Split-Path $dest)|Out-Null;Copy-Item $f.FullName $dest}
if($Kind -eq 'canonical'){Copy-Item "$source/unity/HumanVisionDemo/Assets/HumanVision/Demo/*" "$project/Assets/HumanVision/Demo" -Recurse}
Copy-Item "$source/tools/test/Task9AdapterGate.cs" "$project/Assets/";Copy-Item "$source/tools/test/Task9AdapterGateBuild.cs" "$project/Assets/Editor/"
$runtimeData="$project/Assets/StreamingAssets/HumanVision/Runtime";$pack="$repo/out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16"
Copy-Item $pack "$runtimeData/modelpacks/precision-t-26-ncnn-fp16" -Recurse
Copy-Item "$source/profiles/android-ncnn-vulkan.json" "$runtimeData/profiles/"
$packManifest=Get-Content "$pack/modelpack.json" -Raw|ConvertFrom-Json
if($packManifest.profile_sha256 -ne (Get-FileHash "$runtimeData/profiles/android-ncnn-vulkan.json").Hash.ToLowerInvariant()){throw 'Unchanged model/profile binding mismatch'}
$index=@();foreach($f in Get-ChildItem $runtimeData -Recurse -File){$index+=@{path=($f.FullName.Substring($runtimeData.Length+1)-replace '\\','/');sha256=(Get-FileHash $f.FullName).Hash.ToLowerInvariant()}}
@{version='task9-frozen-qualified-pack';files=$index}|ConvertTo-Json -Depth 5|Set-Content "$runtimeData/index.json"
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop();$url="rtsp://127.0.0.1:$port/fixture";$url|Set-Content "$project/Assets/Resources/input-gate-url.txt"
$manifest=Get-Content "$source/unity/HumanVisionDemo/Packages/manifest.json" -Raw|ConvertFrom-Json;$manifest.dependencies|Add-Member -NotePropertyName 'com.blazetc.humanvision.input' -NotePropertyValue ('file:'+((Join-Path $source 'upm/com.blazetc.humanvision.input')-replace '\\','/'));$manifest|ConvertTo-Json -Depth 5|Set-Content "$project/Packages/manifest.json"
'm_EditorVersion: 2021.3.45f1'|Set-Content "$project/ProjectSettings/ProjectVersion.txt"
'<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.blazetc.humanvision.task9gate"><uses-permission android:name="android.permission.INTERNET"/><application android:usesCleartextTraffic="true"><meta-data android:name="com.blazetc.humanvision.runtime_mode" android:value="android-ncnn-vulkan"/><meta-data android:name="com.blazetc.humanvision.profile_id" android:value="android-ncnn-vulkan"/><activity android:name="com.unity3d.player.UnityPlayerActivity" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter><meta-data android:name="unityplayer.UnityActivity" android:value="true"/></activity></application></manifest>'|Set-Content "$project/Assets/Plugins/Android/AndroidManifest.xml"
$apk="$out/task9-adapter.apk";$unity='D:/Developer/2021.3.45f1/Editor/Unity.exe'
$arguments="-batchmode -nographics -projectPath `"$project`" -executeMethod Task9AdapterGateBuild.Build -task9Apk `"$apk`" -logFile `"$out/unity-build.log`" -quit"
$p=Start-Process $unity -ArgumentList $arguments -WindowStyle Hidden -PassThru;$p.WaitForExit();$p.Refresh();if($p.ExitCode -ne 0 -or -not(Test-Path $apk)){throw 'Actual Android conditional source/APK build failed'}
$sources=@{};foreach($f in Get-ChildItem "$project/Assets" -Recurse -File){if($f.Extension -in @('.cs','.asmdef','.so','.json','.param','.bin')){$sources[($f.FullName.Substring($project.Length+1)-replace '\\','/')]=(Get-FileHash $f.FullName).Hash.ToLowerInvariant()}}
@{source_root=$source;kind=$Kind;apk_sha256=(Get-FileHash $apk).Hash.ToLowerInvariant();native_sha256=(Get-FileHash "$native/bin/Release/libhumanvision.so").Hash.ToLowerInvariant();input_native_sha256=$inputReceipt.native_sha256;model_root=$pack;profile_sha256=$packManifest.profile_sha256;manifest_sha256=(Get-FileHash "$pack/modelpack.json").Hash.ToLowerInvariant();source_geometry='640x360';fixture_fps=25;detector_model_geometry='320x320';pose_model_geometry='192x256';cadence=4;ndk='23.1.7779620';api=26;sources=$sources}|ConvertTo-Json -Depth 6|Set-Content "$out/build-receipt.json"
if($BuildOnly){Write-Output "Actual $Kind Android build PASS: $apk";return}
}else{
 $receipt=Get-Content "$out/build-receipt.json" -Raw|ConvertFrom-Json
 if($receipt.source_root -ne $source){throw 'Replay source root mismatch'}
 $apk="$out/task9-adapter.apk"
 if((Get-FileHash $apk).Hash.ToLowerInvariant() -ne $receipt.apk_sha256){throw 'Replay APK hash mismatch'}
 $url=(Get-Content "$project/Assets/Resources/input-gate-url.txt" -Raw).Trim();$port=([Uri]$url).Port
}
$adb='D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe';$server="$repo/out/tools/mediamtx-v1.12.3/mediamtx.exe";$ffmpeg='C:/ffmpeg/bin/ffmpeg.exe';$fixture="$repo/out/input/task9-fixture/dance-video-1-h264.mp4"
$lock=(& py -3 "$repo/tools/test/rtsp_fixture_manifest.py"|ConvertFrom-Json)
if((Get-FileHash $server).Hash.ToLowerInvariant() -ne $lock.mediamtx_sha256 -or (Get-FileHash $ffmpeg).Hash.ToLowerInvariant() -ne $lock.ffmpeg_sha256){throw 'Controlled fixture tool pin mismatch'}
@{fixture=$fixture;sha256=(Get-FileHash $fixture).Hash.ToLowerInvariant();source_video_sha256=$lock.video_sha256;source_segment='video-1 original37..49s scaled640x360 with32pixel corner markers';fps=25}|ConvertTo-Json|Set-Content "$out/fixture-receipt.json"
$owned=@();$reverse=$false;$pass=$false;$videoPid=0
function Save-ActiveGateScreen([string]$Name){
 $foreground=(& $adb -s $Serial shell dumpsys activity activities)-join "`n";$foreground|Set-Content "$out/$Name-foreground.txt"
 if($foreground -notmatch '(?:mResumedActivity:|topResumedActivity=|ResumedActivity:)[^\r\n]*com\.blazetc\.humanvision\.task9gate/'){throw 'Screenshot rejected: exact gate package is not resumed'}
 & $adb -s $Serial shell screencap -p "/sdcard/task9-$Name.png";& $adb -s $Serial pull "/sdcard/task9-$Name.png" "$out/$Name.png" > "$out/$Name-pull.log" 2>&1
 if($LASTEXITCODE -ne 0){throw 'Active gate screenshot pull failed'}
}
try {
 & $adb -s $Serial install -r $apk > "$out/install.log";if($LASTEXITCODE -ne 0){throw 'Device APK install failed'}
 $remote=(& $adb -s $Serial shell pm path com.blazetc.humanvision.task9gate|Select-Object -First 1)-replace '^package:',''; & $adb -s $Serial pull $remote.Trim() "$out/installed-base.apk" > "$out/installed-pull.log" 2>&1
 if((Get-FileHash "$out/installed-base.apk").Hash -ne (Get-FileHash $apk).Hash){throw 'Installed APK hash mismatch'}
 & $adb -s $Serial logcat -c;& $adb -s $Serial reverse "tcp:$port" "tcp:$port";$reverse=$true
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
"@|Set-Content "$out/mediamtx.yml"
 $p=Start-Process $server -ArgumentList "`"$out/mediamtx.yml`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/server.stdout.log" -RedirectStandardError "$out/server.stderr.log";$owned+=New-InputGateProcessIdentity $p $server
 Start-Sleep -Milliseconds 800
 $p=Start-Process $ffmpeg -ArgumentList "-hide_banner -nostdin -stream_loop -1 -re -i `"$fixture`" -an -c:v copy -f rtsp -rtsp_transport tcp $url" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/publisher.stdout.log" -RedirectStandardError "$out/publisher.stderr.log";$owned+=New-InputGateProcessIdentity $p $ffmpeg
 Start-Sleep -Seconds 2;& $adb -s $Serial shell am force-stop com.blazetc.humanvision.task9gate;& $adb -s $Serial shell monkey -p com.blazetc.humanvision.task9gate 1 > "$out/launch.log"
 $watch=[Diagnostics.Stopwatch]::StartNew();$recognition=$false;$detach=$false;$live=$false;$errorCapture=$false
 do{Start-Sleep -Milliseconds 500;& $adb -s $Serial logcat -d > "$out/logcat.txt";$raw=Get-Content "$out/logcat.txt" -Raw
  if(!$live -and $raw -match 'HVTask9 live preview=[0-9]+'){Save-ActiveGateScreen 'live-preview';& $adb -s $Serial shell dumpsys SurfaceFlinger --list > "$out/surface-list.txt";& $adb -s $Serial shell dumpsys gfxinfo com.blazetc.humanvision.task9gate framestats > "$out/gfxinfo-framestats.txt";
   $videoPid=[int]((& $adb -s $Serial shell 'nohup /system/bin/screenrecord --time-limit 10 --bit-rate 4000000 /sdcard/task9-live.mp4 >/sdcard/task9-screenrecord.log 2>&1 & echo $!'|Select-Object -Last 1).Trim());@{pid=$videoPid;host_elapsed_s=$watch.Elapsed.TotalSeconds;time_limit_s=10;command='screenrecord --time-limit10 /sdcard/task9-live.mp4'}|ConvertTo-Json|Set-Content "$out/screenrecord-start.json";$live=$true}
  if(!$errorCapture -and $raw -match 'HVTask9 live[^\r\n]+adapter_error=\S|HVTask9 result=FAIL'){Save-ActiveGateScreen 'live-error';$errorCapture=$true}
  if(!$recognition -and $raw -match 'HVTask9 recognition_ready'){Save-ActiveGateScreen 'recognition';$recognition=$true}
  if(!$detach -and $raw -match 'HVTask9 detach_preview_continues'){Save-ActiveGateScreen 'detached-preview';$detach=$true}
 }while($watch.Elapsed.TotalSeconds -lt 130 -and $raw -notmatch 'HVTask9 result=(PASS|FAIL)')
 $pass=$raw -match 'HVTask9 result=PASS' -and $recognition -and $detach
 if($videoPid -gt 0){& $adb -s $Serial pull /sdcard/task9-live.mp4 "$out/live-display.mp4" > "$out/video-pull.log" 2>&1;& $adb -s $Serial pull /sdcard/task9-screenrecord.log "$out/screenrecord-device.log" > "$out/screenrecord-log-pull.log" 2>&1}
 @{status=$(if($pass){'PASS'}else{'FAIL'});recognition=$recognition;detach_preview=$detach;live_capture=$live;error_capture=$errorCapture}|ConvertTo-Json|Set-Content "$out/analysis.json"
 if(!$pass){throw "Actual physical adapter gate failed: $out/logcat.txt"}
}finally{
 if($videoPid -gt 0){$line=(& $adb -s $Serial shell "cat /proc/$videoPid/cmdline" 2>&1)-join '';if($line -match 'screenrecord' -and $line -match '/sdcard/task9-live.mp4'){& $adb -s $Serial shell kill $videoPid > "$out/screenrecord-owned-stop.log" 2>&1}else{$line|Set-Content "$out/screenrecord-exited-or-identity-mismatch.txt"}}
 @{status=$(if($pass){'PASS'}else{'FAIL'});recognition=$recognition;detach_preview=$detach;live_capture=$live;error_capture=$errorCapture}|ConvertTo-Json|Set-Content "$out/analysis.json"
 & $adb -s $Serial shell am force-stop com.blazetc.humanvision.task9gate > "$out/force-stop.log" 2>&1
 $cleanup=@();foreach($identity in $owned){$cleanup+=Stop-InputGateOwnedProcess $identity};$cleanup|ConvertTo-Json|Set-Content "$out/owned-process-cleanup.json"
 if($reverse){& $adb -s $Serial reverse --remove "tcp:$port" > "$out/reverse-remove.log" 2>&1}
}
Write-Output "Actual physical adapter gate PASS: $out"
