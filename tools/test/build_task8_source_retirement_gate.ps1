param([Parameter(Mandatory=$true)][string]$SourceRoot,[Parameter(Mandatory=$true)][string]$Url,
      [string]$Output='out/input/task8-device',[string]$Unity='D:/Developer/2021.3.45f1/Editor/Unity.exe')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=(Resolve-Path -LiteralPath $SourceRoot).Path
$out=[IO.Path]::GetFullPath((Join-Path $repo $Output))
if(-not $out.StartsWith("$repo\out\input\",[StringComparison]::OrdinalIgnoreCase)){throw 'Task8 output must be inside out/input'}
$project=Join-Path $out 'project';$native=Join-Path $out 'native-gate'
$cmake='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$ndk='D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK'
if((Get-Content "$ndk/source.properties" -Raw)-notmatch 'Pkg.Revision\s*=\s*23\.1\.7779620'){throw 'Task8 requires locked NDK23.1.7779620'}
foreach($dir in @($out,"$project/Assets/Editor","$project/Assets/Plugins/Android/arm64-v8a","$project/Assets/Resources","$project/ProjectSettings","$project/Packages")){New-Item -ItemType Directory -Force $dir|Out-Null}
& $cmake -S $source -B $native -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHV_ENABLE_RTSP=OFF -DHV_ANDROID_GPU_GATE=ON "-DHV_NCNN_ROOT=$repo/out/ncnn-20260526/android-arm64-api26/install" "-DHV_ONNXRUNTIME_ROOT=$repo/out/live-deps/ort-android" *> "$out/native-configure.log"
if($LASTEXITCODE -ne 0){throw 'Task8 clean candidate native configure failed'}
& $cmake --build $native --target humanvision *> "$out/native-build.log"
if($LASTEXITCODE -ne 0){throw 'Task8 clean candidate native build failed'}
$library="$native/bin/Release/libhumanvision.so"
$plugins="$project/Assets/Plugins/Android/arm64-v8a"
& pwsh -NoProfile -File "$repo/tools/test/verify_android_gpu_bridge_gate_libs.ps1" -Mode Stage -NativeLibrary $library -PluginDirectory $plugins *> "$out/sdk-elf-audit.log"
if($LASTEXITCODE -ne 0){throw 'Task8 SDK API26 closure audit failed'}
$receipt=Get-Content "$repo/out/input-native/android/build-source-identity.json" -Raw|ConvertFrom-Json
if($receipt.native_sha256 -ne (Get-FileHash "$repo/out/input-native/android/libhumanvision_input.so").Hash.ToLowerInvariant()){throw 'Qualified input library hash changed'}
foreach($entry in $receipt.sources.PSObject.Properties){if((Get-FileHash (Join-Path $repo $entry.Name)).Hash.ToLowerInvariant() -ne $entry.Value){throw "Qualified input source changed: $($entry.Name)"}}
Copy-Item "$repo/out/input-native/android/libhumanvision_input.so" "$plugins/libhumanvision_input.so"
foreach($lib in Get-ChildItem "$repo/out/live-deps/ffmpeg-android" -Filter '*.so'){Copy-Item $lib.FullName $plugins -Force}
foreach($file in Get-ChildItem "$source/upm/com.blazetc.humanvision.input/Runtime" -Filter '*.cs'){Copy-Item $file.FullName "$project/Assets/$($file.Name)"}
Copy-Item "$source/upm/com.blazetc.humanvision.input/Runtime/Resources/HumanVisionInputOrientation.shader" "$project/Assets/Resources/"
$fixture="$source/tools/test/fixtures/unity_task8_source_retirement"
Copy-Item "$fixture/Task8SourceRetirementProbe.cs" "$project/Assets/"
Copy-Item "$fixture/Task8SourceRetirementBuild.cs" "$project/Assets/Editor/"
$Url | Set-Content "$project/Assets/Resources/input-gate-url.txt" -Encoding utf8
'{"dependencies":{"com.unity.ugui":"1.0.0","com.unity.modules.androidjni":"1.0.0","com.unity.modules.jsonserialize":"1.0.0","com.unity.modules.imgui":"1.0.0","com.unity.modules.video":"1.0.0","com.unity.modules.audio":"1.0.0"}}'|Set-Content "$project/Packages/manifest.json"
'm_EditorVersion: 2021.3.45f1'|Set-Content "$project/ProjectSettings/ProjectVersion.txt"
$apk=Join-Path $out 'task8-source-retirement.apk'
$arguments="-batchmode -nographics -projectPath `"$project`" -executeMethod Task8SourceRetirementBuild.Build -task8Apk `"$apk`" -logFile `"$out/unity-build.log`" -quit"
$process=Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if($process.ExitCode -ne 0 -or -not(Test-Path $apk)){throw 'Task8 actual Unity APK build failed'}
$hashes=[ordered]@{sdk_native=(Get-FileHash $library).Hash.ToLowerInvariant();input_native=$receipt.native_sha256;apk=(Get-FileHash $apk).Hash.ToLowerInvariant();source_root=$source;ndk='23.1.7779620';api=26;boundary='private producer + real ConsumerFrame occupancy, no model inference';guard='HV_ANDROID_GPU_GATE=ON'}
$hashes|ConvertTo-Json -Depth 5|Set-Content "$out/build-receipt.json"
Write-Output "Task8 candidate APK: $apk"
