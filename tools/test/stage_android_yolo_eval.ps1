param(
    [ValidateSet(320,416)][int]$Size = 320,
    [Parameter(Mandatory)][string]$NativeLibrary,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$RuntimeDirectory = '',
    [string]$Video = 'E:/Project/Human Vision SDK/video-1.mp4'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
if (!$RuntimeDirectory) { $RuntimeDirectory = Join-Path $root "out/android-yolo/runtime-square$Size-verified" }
$native = (Resolve-Path -LiteralPath $NativeLibrary).Path
if (Test-Path -LiteralPath $OutputDirectory) { throw 'OutputDirectory must be new' }
# Audit the actual explicit artifact before writing any successful audit metadata.
& py -3 (Join-Path $PSScriptRoot 'verify_android_native.py') --library $native
if ($LASTEXITCODE -ne 0) { throw 'Explicit Android native audit failed' }
& py -3 (Join-Path $PSScriptRoot 'stage_android_yolo_eval.py') --runtime $RuntimeDirectory --native $native --video $Video --output $OutputDirectory --size $Size
if ($LASTEXITCODE -ne 0) { throw 'YOLO runtime/project stage failed' }
$project = Join-Path $OutputDirectory 'UnityProject'
$plugins = Join-Path $project 'Assets/Plugins/Android/arm64-v8a'
& pwsh -NoProfile -File (Join-Path $PSScriptRoot 'verify_android_gpu_bridge_gate_libs.ps1') -Mode Stage -PluginDirectory $plugins -NativeLibrary $native
if ($LASTEXITCODE -ne 0) { throw 'Explicit native dependency stage failed' }
$nativeHash = (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant()
if ((Get-FileHash -LiteralPath (Join-Path $plugins 'libhumanvision.so') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $nativeHash) { throw 'Copied native differs' }
@{audit='tools/test/verify_android_native.py';native_sha256=$nativeHash;abi='arm64-v8a';api_level=26;ncnn_vulkan_symbols_verified=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project 'android-gpu-bridge-symbols.json') -Encoding utf8
Write-Output "Verified YOLO evaluation project: $project"
