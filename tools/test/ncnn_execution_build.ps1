param(
    [string]$Destination = 'out/android-yolo/execution-diagnostic-20261001',
    [string]$AndroidNdk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
    [ValidateRange(1,64)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$destinationPath = [IO.Path]::GetFullPath((Join-Path $root $Destination)).Replace('\','/')
if (-not $destinationPath.StartsWith("$root/out/", [StringComparison]::OrdinalIgnoreCase)) { throw 'Diagnostic path must be under workspace out' }
$pinPath = "$root/third_party/ncnn/provenance.json"
$pin = Get-Content -LiteralPath $pinPath -Raw | ConvertFrom-Json
if ((Get-Content "$AndroidNdk/source.properties" -Raw) -notmatch "Pkg.Revision\s*=\s*$([regex]::Escape($pin.ndk_version))(\s|$)") { throw 'Pinned NDK version mismatch' }
$cmake = 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
if (-not (Test-Path "$destinationPath/diagnostic-source.json")) {
    & py -3 "$PSScriptRoot/ncnn_execution_trace.py" --prepare $destinationPath
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic source preparation failed' }
}
$diagnostic = Get-Content "$destinationPath/diagnostic-source.json" -Raw | ConvertFrom-Json
if ($diagnostic.script_sha256 -ne (Get-FileHash "$PSScriptRoot/ncnn_execution_trace.py").Hash.ToLowerInvariant()) { throw 'Diagnostic generator drift' }
foreach ($file in $diagnostic.files.PSObject.Properties) {
    if ((Get-FileHash "$destinationPath/source/$($file.Name)").Hash.ToLowerInvariant() -ne $file.Value.after_sha256) { throw "Diagnostic source drift: $($file.Name)" }
}
if ((Get-FileHash "$destinationPath/source-manifest.json").Hash.ToLowerInvariant() -ne $diagnostic.source_manifest_sha256) { throw 'Source manifest drift' }
$manifest = Get-Content "$destinationPath/source-manifest.json" -Raw | ConvertFrom-Json
foreach ($file in $manifest.PSObject.Properties) {
    if ((Get-FileHash "$destinationPath/source/$($file.Name)").Hash.ToLowerInvariant() -ne $file.Value) { throw "Copied source drift: $($file.Name)" }
}
if (@(Get-ChildItem "$destinationPath/source" -File -Recurse).Count -ne $diagnostic.source_file_count) { throw 'Unexpected copied source files' }
$build = "$destinationPath/build"
$install = "$build/install"
$configure = @('-S', "$destinationPath/source", '-B', $build, '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake", '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-26',
    "-DCMAKE_INSTALL_PREFIX=$install", '-DNCNN_BENCHMARK=ON', '-DCMAKE_CXX_FLAGS=-DHV_NCNN_EXECUTION_DIAGNOSTIC=1')
foreach ($flag in $pin.build_flags.PSObject.Properties) { $configure += "-D$($flag.Name)=$($flag.Value)" }
& $cmake @configure > "$destinationPath/configure.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "Diagnostic configure failed: $destinationPath/configure.log" }
& $cmake --build $build --target install --parallel $Jobs > "$destinationPath/build.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "Diagnostic build failed: $destinationPath/build.log" }
$receipt = [ordered]@{
    provenance_sha256 = (Get-FileHash $pinPath).Hash.ToLowerInvariant()
    source_commit = $pin.source_commit; archive_sha256 = $pin.archive.sha256
    patches = $pin.patches; android_abi = 'arm64-v8a'; android_api_level = 26
    build_flags = $pin.build_flags; ndk_version = $pin.ndk_version
    execution_diagnostic = $diagnostic
    libraries = @(Get-ChildItem "$install/lib" -Filter '*.a' | ForEach-Object {
        @{path="lib/$($_.Name)"; sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()}
    })
}
$receipt | ConvertTo-Json -Depth 12 | Set-Content "$build/build-receipt.json" -Encoding utf8
Write-Output "Separate diagnostic ncnn ready: $install"
