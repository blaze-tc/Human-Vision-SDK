param(
    [string]$VisualStudio = 'D:/Microsoft Visual Studio',
    [string]$AndroidNdk = 'D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$cmake = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ninja = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
$batch = @"
@echo off
call "$VisualStudio/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
"$cmake" -S "$root" -B "$root/build/windows-live" -G "Ninja Multi-Config" -DBUILD_TESTING=OFF -DHV_USE_DIRECTML=ON -DHV_ENABLE_RTSP=ON -DHV_ONNXRUNTIME_ROOT="$root/out/hv-ort-dml" -DHV_DIRECTML_ROOT="$root/out/directml-1.15.4" -DHV_FFMPEG_INCLUDE="$root/out/live-deps/ffmpeg-headers" -DHV_FFMPEG_LIB_DIR="$root/out/live-deps/ffmpeg-windows"
if errorlevel 1 exit /b 1
"$cmake" --build "$root/build/windows-live" --config Release --target humanvision
"@
$batchPath = "$root/out/build-live-windows.cmd"
$batch | Set-Content $batchPath -Encoding ascii
& cmd /d /c "`"$batchPath`"" > "$root/out/build-live-windows.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Windows native build failed; see out/build-live-windows.log' }
& $cmake -S $root -B "$root/build/android-live" -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHV_ENABLE_RTSP=ON "-DHV_ONNXRUNTIME_ROOT=$root/out/live-deps/ort-android" "-DHV_FFMPEG_INCLUDE=$root/out/live-deps/ffmpeg-headers" "-DHV_FFMPEG_LIB_DIR=$root/out/live-deps/ffmpeg-android" > "$root/out/configure-android-live.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Android configuration failed' }
& $cmake --build "$root/build/android-live" --target humanvision > "$root/out/build-android-live.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Android native build failed' }
Write-Output 'Windows x64 and Android ARM64 native libraries built. Tests were not run.'
