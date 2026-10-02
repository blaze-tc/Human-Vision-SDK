param([ValidateSet('Capabilities')][string]$Gate='Capabilities',[string]$Serial='e7c07019',
      [string]$Output='out/input/task5-device',[string]$Unity='D:/Developer/2021.3.45f1/Editor/Unity.exe')
$ErrorActionPreference='Stop'
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
$lock=(& py -3.13 "$PSScriptRoot/rtsp_fixture_manifest.py")|ConvertFrom-Json
foreach($pair in @(@($ffmpeg,$lock.ffmpeg_sha256),@($server,$lock.mediamtx_sha256))){if((Get-FileHash $pair[0]).Hash.ToLowerInvariant() -ne $pair[1]){throw 'Controlled fixture tool qualification failed'}}
if((Get-FileHash $fixture).Hash -ne '926DF205B1A8D3E1CF3E0EBEBBDC4E9232F5A0F8D2EB65C2B918C372F2668CFC'){throw 'Task4 controlled H264 fixture identity mismatch'}
& $adb -s $Serial get-state > "$run/adb-state.txt"
if($LASTEXITCODE -ne 0){throw 'Authorized Android device unavailable'}
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
"rtsp://127.0.0.1:$port/fixture" | Set-Content "$project/Assets/Resources/input-gate-url.txt" -Encoding utf8
'{"dependencies":{"com.unity.modules.androidjni":"1.0.0"}}' | Set-Content "$project/Packages/manifest.json" -Encoding utf8
'm_EditorVersion: 2021.3.45f1' | Set-Content "$project/ProjectSettings/ProjectVersion.txt"
Copy-Item "$repo/upm/com.blazetc.humanvision.input/Tests/PlayMode/AndroidInputCapabilityProbe.cs" "$project/Assets/AndroidInputCapabilityProbe.cs" -Force
Copy-Item "$PSScriptRoot/AndroidInputCapabilityBuild.cs" "$project/Assets/Editor/AndroidInputCapabilityBuild.cs" -Force
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" "$project/Assets/Plugins/Android/arm64-v8a/libhumanvision_input.so" -Force
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" "$run/libhumanvision_input.so" -Force
foreach($name in @('avformat','avcodec','avutil','swresample')){Copy-Item "$repo/out/live-deps/ffmpeg-android/lib$name.so" "$project/Assets/Plugins/Android/arm64-v8a/lib$name.so" -Force}
$apk="$run/input-capability.apk"
$owned=@()
function StartOwned($exe,$arguments,$stdout,$stderr) {
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  $identity=@{pid=$process.Id;startTicks=$process.StartTime.ToUniversalTime().Ticks;exe=$process.Path}
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
  & $adb -s $Serial shell monkey -p com.blazetc.humanvision.inputgate 1 > "$run/launch.log"
  Start-Sleep -Seconds 22
  & $adb -s $Serial logcat -d > "$run/device-logcat.txt"
  $raw=Get-Content "$run/device-logcat.txt" -Raw
  $decoded=@([regex]::Matches($raw,'decoded codec=[^\r\n]+') | ForEach-Object {$_.Value})
  $returned=@([regex]::Matches($raw,'decoded_lease_returned=true[^\r\n]+active_images=0 active_ahb_references=0 active_owned_fds=0'))
  $pass=$decoded.Count -eq 3 -and $returned.Count -eq 3 -and @($decoded | Where-Object {$_ -match 'logical_proven=1' -and $_ -match 'query=1 failure=0'}).Count -eq 3 -and $raw -notmatch 'capability_result=FAIL' -and $raw -match 'input_only_preinit_hook=1' -and $raw -match 'device_creation success' -and $raw -match 'sync_fd_properties importable=1 exportable=1' -and $raw -match 'decoder_resources_closed=true active_images=0 active_ahb_references=0 active_owned_fds=0' -and $raw -match 'bounded_probe_closed=true'
  $hashes=[ordered]@{}
  foreach($file in @($apk,"$run/installed-base.apk",$fixture,$server,$ffmpeg,$Unity,$adb,"$run/libhumanvision_input.so")){ $hashes[$file]=(Get-FileHash $file).Hash }
  @{gate=$Gate;serial=$Serial;status=$(if($pass){'PASS'}else{'FAIL'});protocol='RTSP_TCP';codec='H264';port=$port;apk=$apk;project=$project;artifactSha256=$hashes;decodedRecords=$decoded;sourceGeneration='actual native decoded records in device-logcat.txt'} | ConvertTo-Json -Depth 6 | Set-Content "$run/result.json"
  Select-String -Path "$run/device-logcat.txt" -Pattern 'HVInputGate' | ForEach-Object {$_.Line}
  if(-not $pass){throw "Actual Android capability gate FAIL; raw evidence $run"}
} finally {
  & $adb -s $Serial shell am force-stop com.blazetc.humanvision.inputgate | Out-Null
  if($reversed){& $adb -s $Serial reverse --remove "tcp:$port" | Out-Null}
  foreach($identity in $owned){$live=Get-Process -Id $identity.pid -ErrorAction SilentlyContinue;if($live -and $live.StartTime.ToUniversalTime().Ticks -eq $identity.startTicks -and $live.Path -eq $identity.exe){Stop-Process -Id $live.Id -Force}}
  @{reverseRemoved=$reversed;ownedProcesses=$owned;closedUtc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content "$run/cleanup.json"
}
