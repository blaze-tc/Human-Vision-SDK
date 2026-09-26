param(
    [string]$VisualStudio = 'D:/Microsoft Visual Studio',
    [string]$AndroidNdk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
    [ValidateSet('All','Windows','Android')][string]$Platform = 'All',
    [ValidateSet(26)][int]$AndroidApiLevel = 26,
    [switch]$GpuGate,
    [switch]$TopDownEvalTrace,
    [switch]$Fresh
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$cmake = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ninja = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
$freshArg = if ($Fresh) { '--fresh' } else { '' }
$cleanArg = if ($Fresh) { '--clean-first' } else { '' }
New-Item -ItemType Directory -Force "$root/out" | Out-Null
if ($Platform -in @('All','Windows')) {
$batch = @"
@echo off
call "$VisualStudio/Common7/Tools/VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44
if errorlevel 1 exit /b 1
set VSLANG=1033
chcp 65001 >nul
"$cmake" $freshArg -S "$root" -B "$root/build/windows-live" -G "Ninja Multi-Config" -DBUILD_TESTING=OFF -DHV_USE_DIRECTML=ON -DHV_ENABLE_RTSP=ON -DHV_ONNXRUNTIME_ROOT="$root/out/hv-ort-dml" -DHV_DIRECTML_ROOT="$root/out/directml-1.15.4" -DHV_FFMPEG_INCLUDE="$root/out/live-deps/ffmpeg-headers" -DHV_FFMPEG_LIB_DIR="$root/out/live-deps/ffmpeg-windows"
if errorlevel 1 exit /b 1
"$cmake" --build "$root/build/windows-live" --config Release --target humanvision $cleanArg
"@
$batchPath = "$root/out/build-live-windows.cmd"
$batch | Set-Content $batchPath -Encoding ascii
& cmd /d /c "`"$batchPath`"" > "$root/out/build-live-windows.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Windows native build failed; see out/build-live-windows.log' }
}
if ($Platform -in @('All','Android')) {
# The NDK caches CMAKE_SYSTEM_VERSION independently of ANDROID_PLATFORM. Always
# reconfigure from fresh cache so an existing API-24 build cannot survive this pin.
$androidConfigure = @('--fresh', '-S', $root, '-B', "$root/build/android-live", '-G', 'Ninja')
& $cmake @androidConfigure "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$AndroidApiLevel" -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHV_ENABLE_RTSP=ON "-DHV_ANDROID_GPU_GATE=$($GpuGate.IsPresent)" "-DHV_ANDROID_TOPDOWN_EVAL_TRACE=$($TopDownEvalTrace.IsPresent)" "-DHV_ONNXRUNTIME_ROOT=$root/out/live-deps/ort-android" "-DHV_FFMPEG_INCLUDE=$root/out/live-deps/ffmpeg-headers" "-DHV_FFMPEG_LIB_DIR=$root/out/live-deps/ffmpeg-android" > "$root/out/configure-android-live.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Android configuration failed' }
& $cmake --build "$root/build/android-live" --target humanvision > "$root/out/build-android-live.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw 'Android native build failed' }
}
switch ($Platform) {
    'All' { Write-Output "Windows x64 and Android ARM64 API $AndroidApiLevel native libraries built. Tests were not run." }
    'Android' { Write-Output "Android ARM64 API $AndroidApiLevel native libraries built. Tests were not run." }
    'Windows' { Write-Output 'Windows x64 native libraries built. Tests were not run.' }
}
