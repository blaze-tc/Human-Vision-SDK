param(
    [string]$ProjectPath = 'unity/HumanVisionDemo',
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [switch]$Prepared
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$source = (Resolve-Path (Join-Path $root $ProjectPath)).Path
$out = Join-Path $root $(if ($Prepared) { 'out/android-prepared-gate-runtime' } else { 'out/android-gpu-gate-runtime' })
$project = Join-Path $out 'UnityProject'
New-Item -ItemType Directory -Path $out, $project -Force | Out-Null
$fixture = Join-Path $out 'input-contract.json'
if ($Prepared) {
    $packRoot = Join-Path $root 'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
    $pack = Get-Content -LiteralPath (Join-Path $packRoot 'modelpack.json') -Raw | ConvertFrom-Json
    $pack | Add-Member -NotePropertyName active_role -NotePropertyValue detector -Force
    $pack | Add-Member -NotePropertyName asset_root -NotePropertyValue '__ASSET_ROOT__' -Force
    $pack | ConvertTo-Json -Depth 100 -Compress | Set-Content -LiteralPath $fixture -Encoding utf8
} else {
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
}
& pwsh -NoProfile -File (Join-Path $root 'tools/package/build_live_native.ps1') -Platform Android -AndroidApiLevel 26 -GpuGate
if ($LASTEXITCODE -ne 0) { throw 'Native gate build failed' }
foreach ($folder in @('Assets/HumanVision','Assets/Scenes','Assets/StreamingAssets','ProjectSettings','Packages')) {
    $destination = Join-Path $project $folder
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    $origin = Join-Path $source $folder
    if (Test-Path -LiteralPath $origin) { Copy-Item -Path (Join-Path $origin '*') -Destination $destination -Recurse -Force }
}
if ($Prepared) {
    $targetDetector = Join-Path $project 'Assets/StreamingAssets/HumanVisionPreparedGate/detector'
    New-Item -ItemType Directory -Path $targetDetector -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $packRoot 'detector/model.param') -Destination $targetDetector -Force
    Copy-Item -LiteralPath (Join-Path $packRoot 'detector/model.bin') -Destination $targetDetector -Force
}
$plugins = Join-Path $project 'Assets/Plugins/Android/arm64-v8a'
& pwsh -NoProfile -File (Join-Path $root 'tools/test/verify_android_gpu_bridge_gate_libs.ps1') -Mode Stage -PluginDirectory $plugins
if ($LASTEXITCODE -ne 0) { throw 'Failed to stage Android ARM64 dependency closure' }
$log = Join-Path $out 'unity-build.log'
$marker = if ($Prepared) { '-humanvisionPreparedGate' } else { '-humanvisionGpuGate' }
$arguments = "-batchmode -nographics $marker -projectPath `"$project`" -executeMethod HumanVision.Editor.HumanVisionAndroidGpuGateBuild.Build -logFile `"$log`" -quit"
$started = [DateTime]::UtcNow
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Unity gate build failed ($($process.ExitCode)); see $log" }
$gatePluginMeta = Join-Path $plugins 'libhumanvision.so.meta'
if (!(Test-Path -LiteralPath $gatePluginMeta) -or
    (Get-Content -LiteralPath $gatePluginMeta -Raw) -notmatch '(?m)^  isPreloaded: 1\s*$') {
    throw "Gate libhumanvision.so is not preloaded: $gatePluginMeta"
}
$apk = Join-Path $out $(if ($Prepared) { 'humanvision-prepared-gate.apk' } else { 'humanvision-gpu-bridge-gate.apk' })
if (!(Test-Path -LiteralPath $apk) -or (Get-Item -LiteralPath $apk).LastWriteTimeUtc -lt $started) { throw 'Gate APK missing or stale' }
& pwsh -NoProfile -File (Join-Path $root 'tools/test/verify_android_gpu_bridge_gate_libs.ps1') -Mode Verify -ApkPath $apk
if ($LASTEXITCODE -ne 0) { throw 'Gate APK Android ARM64 dependency audit failed' }
$sha = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output "GPU gate APK: $apk"
Write-Output "SHA-256: $sha"
Write-Output "Input contract: $fixture"
