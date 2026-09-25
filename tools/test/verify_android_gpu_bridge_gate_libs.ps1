param(
    [ValidateSet('Stage','Verify')][string]$Mode = 'Verify',
    [string]$PluginDirectory = 'out/android-gpu-gate-runtime/UnityProject/Assets/Plugins/Android/arm64-v8a',
    [string]$ApkPath = 'out/android-gpu-gate-runtime/humanvision-gpu-bridge-gate.apk',
    [string]$AndroidNdk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
function AbsolutePath([string]$path) {
    if ([IO.Path]::IsPathRooted($path)) { return $path }
    return Join-Path $root $path
}
$pluginPath = AbsolutePath $PluginDirectory
$apk = AbsolutePath $ApkPath
$readelf = Join-Path $AndroidNdk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
$systemLibs = Join-Path $AndroidNdk 'toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/26'
if (!(Test-Path -LiteralPath $readelf)) { throw "Missing Android NDK llvm-readelf: $readelf" }

$candidates = @{}
$native = Join-Path $root 'build/android-live/bin/Release/libhumanvision.so'
$ort = Join-Path $root 'out/live-deps/ort-android/lib/libonnxruntime.so'
$ffmpeg = Join-Path $root 'out/live-deps/ffmpeg-android'
foreach ($path in @($native, $ort)) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing Android ARM64 library: $path" }
    $candidates[[IO.Path]::GetFileName($path)] = $path
}
if (!(Test-Path -LiteralPath $ffmpeg)) { throw "Missing pinned Android FFmpeg directory: $ffmpeg" }
foreach ($file in Get-ChildItem -LiteralPath $ffmpeg -Filter '*.so' -File) {
    if ($candidates.ContainsKey($file.Name)) { throw "Duplicate Android library candidate: $($file.Name)" }
    $candidates[$file.Name] = $file.FullName
}

$required = @{}
$pending = [Collections.Generic.Queue[string]]::new()
$pending.Enqueue('libhumanvision.so')
while ($pending.Count -gt 0) {
    $name = $pending.Dequeue()
    if ($required.ContainsKey($name)) { continue }
    if (!$candidates.ContainsKey($name)) { throw "Missing Android dependency source: $name" }
    $path = $candidates[$name]
    $header = & $readelf -h $path 2>&1
    if ($LASTEXITCODE -ne 0 -or ($header -join "`n") -notmatch 'AArch64') { throw "Not an Android ARM64 ELF library: $path" }
    $dynamic = & $readelf -d $path 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Cannot read ELF dependencies: $path" }
    $required[$name] = $path
    foreach ($line in $dynamic) {
        if ($line -match '\(NEEDED\).*\[([^\]]+)\]') {
            $dependency = $Matches[1]
            if ($candidates.ContainsKey($dependency)) { $pending.Enqueue($dependency) }
            elseif (!(Test-Path -LiteralPath (Join-Path $systemLibs $dependency))) {
                throw "Unresolved Android API 26 dependency $dependency required by $name"
            }
        }
    }
}
$names = @($required.Keys | Sort-Object)
if ($Mode -eq 'Stage') {
    New-Item -ItemType Directory -Path $pluginPath -Force | Out-Null
    Get-ChildItem -LiteralPath $pluginPath -Filter '*.so' -File | Remove-Item -Force
    foreach ($name in $names) { Copy-Item -LiteralPath $required[$name] -Destination (Join-Path $pluginPath $name) -Force }
    Write-Output "Staged ARM64 dependency closure: $($names -join ', ')"
    return
}
if (!(Test-Path -LiteralPath $apk)) { throw "Missing gate APK: $apk" }
Add-Type -AssemblyName System.IO.Compression
$archive = [IO.Compression.ZipFile]::OpenRead($apk)
try {
    foreach ($name in $names) {
        $entryName = "lib/arm64-v8a/$name"
        $entry = $archive.GetEntry($entryName)
        if ($null -eq $entry) { throw "Gate APK missing required ARM64 library: $name" }
        $stream = $entry.Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $actual = [Convert]::ToHexString($hasher.ComputeHash($stream)) }
        finally { $hasher.Dispose(); $stream.Dispose() }
        $expected = (Get-FileHash -LiteralPath $required[$name] -Algorithm SHA256).Hash
        if ($actual -ne $expected) { throw "Gate APK library differs from pinned build: $name" }
    }
} finally { $archive.Dispose() }
Write-Output "Verified APK ARM64 dependency closure ($($names.Count)): $($names -join ', ')"
