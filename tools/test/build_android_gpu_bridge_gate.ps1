param(
    [string]$ProjectPath = 'unity/HumanVisionDemo',
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$source = (Resolve-Path (Join-Path $root $ProjectPath)).Path
$out = Join-Path $root 'out/android-gpu-gate-runtime'
$project = Join-Path $out 'UnityProject'
New-Item -ItemType Directory -Path $out, $project -Force | Out-Null
$fixture = Join-Path $out 'input-contract.json'
@'
{
  "image_format": "rgba8-unorm",
  "color_order": "rgb",
  "normalization": {"mean": [0.0, 0.0, 0.0], "norm": [0.0039215686, 0.0039215686, 0.0039215686]},
  "tensor_dtype": "fp16",
  "elempack": 4,
  "width": 320,
  "height": 320,
  "input_blob": "gate_input_only",
  "output_blobs": ["gate_no_model_output"]
}
'@ | Set-Content -LiteralPath $fixture -Encoding utf8
& pwsh -NoProfile -File (Join-Path $root 'tools/package/build_live_native.ps1') -Platform Android -AndroidApiLevel 26 -GpuGate
if ($LASTEXITCODE -ne 0) { throw 'Native gate build failed' }
foreach ($folder in @('Assets/HumanVision','Assets/Scenes','Assets/StreamingAssets','ProjectSettings','Packages')) {
    $destination = Join-Path $project $folder
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $origin = Join-Path $source $folder
    if (Test-Path -LiteralPath $origin) { Copy-Item -Path (Join-Path $origin '*') -Destination $destination -Recurse -Force }
}
$plugins = Join-Path $project 'Assets/Plugins/Android/arm64-v8a'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'build/android-live/bin/Release/libhumanvision.so') -Destination (Join-Path $plugins 'libhumanvision.so') -Force
Copy-Item -LiteralPath (Join-Path $root 'out/live-deps/ort-android/lib/libonnxruntime.so') -Destination (Join-Path $plugins 'libonnxruntime.so') -Force
$log = Join-Path $out 'unity-build.log'
$arguments = "-batchmode -nographics -humanvisionGpuGate -projectPath `"$project`" -executeMethod HumanVision.Editor.HumanVisionAndroidGpuGateBuild.Build -logFile `"$log`" -quit"
$started = [DateTime]::UtcNow
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Unity gate build failed ($($process.ExitCode)); see $log" }
$apk = Join-Path $out 'humanvision-gpu-bridge-gate.apk'
if (!(Test-Path -LiteralPath $apk) -or (Get-Item -LiteralPath $apk).LastWriteTimeUtc -lt $started) { throw 'Gate APK missing or stale' }
$sha = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
Add-Type -AssemblyName System.IO.Compression
$archive = [System.IO.Compression.ZipFile]::OpenRead($apk)
try {
    $names = @($archive.Entries | ForEach-Object FullName)
    if ($names -notcontains 'lib/arm64-v8a/libhumanvision.so' -or $names -notcontains 'lib/arm64-v8a/libonnxruntime.so') {
        throw 'Gate APK lacks required ARM64 native libraries'
    }
} finally { $archive.Dispose() }
Write-Output "GPU gate APK: $apk"
Write-Output "SHA-256: $sha"
Write-Output "Input contract: $fixture"
