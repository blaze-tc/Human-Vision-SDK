param(
    [string]$NcnnRoot = 'out/android-yolo/execution-diagnostic-20261001-final/build/install',
    [string]$BuildDirectory = 'build/android-yolo-execution-trace',
    [string]$AndroidNdk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
    [ValidateRange(1,64)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$ncnn = [IO.Path]::GetFullPath((Join-Path $root $NcnnRoot)).Replace('\','/')
$build = [IO.Path]::GetFullPath((Join-Path $root $BuildDirectory)).Replace('\','/')
if (-not $build.StartsWith("$root/build/", [StringComparison]::OrdinalIgnoreCase)) { throw 'Build directory must be under workspace build' }
$cmake = 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
New-Item -ItemType Directory -Force $build | Out-Null
& $cmake -S $root -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake" `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DHV_ENABLE_RTSP=ON `
    "-DHV_NCNN_ROOT=$ncnn" -DHV_ANDROID_TOPDOWN_EVAL_TRACE=ON -DHV_ANDROID_NCNN_EXECUTION_TRACE=ON `
    -DHV_ANDROID_R4_PARITY=OFF -DHV_ANDROID_GPU_GATE=OFF -DHV_USE_QNN=OFF -DHV_USE_DIRECTML=OFF `
    "-DHV_ONNXRUNTIME_ROOT=$root/out/live-deps/ort-android" `
    "-DHV_FFMPEG_INCLUDE=$root/out/live-deps/ffmpeg-headers" "-DHV_FFMPEG_LIB_DIR=$root/out/live-deps/ffmpeg-android" `
    > "$build/configure.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "Runtime configure failed: $build/configure.log" }
& $cmake --build $build --target humanvision --parallel $Jobs > "$build/build.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "Runtime build failed: $build/build.log" }
& py -3 "$root/tools/test/verify_android_native.py" --ndk $AndroidNdk --library "$build/bin/Release/libhumanvision.so" > "$build/api26-audit.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "API26 ARM64 audit failed: $build/api26-audit.log" }
Get-Content "$build/api26-audit.log"
