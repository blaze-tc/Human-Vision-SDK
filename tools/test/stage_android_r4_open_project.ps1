param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$Project = 'E:/UnityProject/Human-Vision-SDK-Test',
    [Parameter(Mandatory)][string]$BackupDirectory
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$projectRoot = (Resolve-Path -LiteralPath $Project).Path
if ($projectRoot -ne 'E:\UnityProject\Human-Vision-SDK-Test') { throw 'This adapter is restricted to the user-authorized open project' }
if (Test-Path -LiteralPath $BackupDirectory) { throw 'Backup directory must be new' }
New-Item -ItemType Directory -Path $BackupDirectory | Out-Null
$backup = (Resolve-Path -LiteralPath $BackupDirectory).Path
$isolated = Join-Path $build 'UnityProject'
$latestNative = Join-Path $root 'build/android-live/bin/Release/libhumanvision.so'
Copy-Item -LiteralPath $latestNative -Destination (Join-Path $isolated 'Assets/Plugins/Android/arm64-v8a/libhumanvision.so') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TopDownEvalBuild.cs') -Destination (Join-Path $isolated 'Assets/HumanVision/Editor/TopDownEvalBuild.cs') -Force
$rows = @()
foreach ($relativeRoot in @('Assets/HumanVision','Assets/Plugins/Android','Assets/StreamingAssets/HumanVision/Runtime','Assets/StreamingAssets/HumanVision/R4','profiles','modelpacks')) {
    $from = Join-Path $isolated $relativeRoot
    if (!(Test-Path -LiteralPath $from)) { throw "Missing staged source: $from" }
    foreach ($file in Get-ChildItem -LiteralPath $from -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($isolated,$file.FullName)
        $target = Join-Path $projectRoot $relative
        $after = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $before = if (Test-Path -LiteralPath $target) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
        if ($before -eq $after) { continue }
        if ($before) {
            $saved = Join-Path $backup $relative
            New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
            if ((Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash.ToLowerInvariant() -ne $before) { throw 'Backup hash mismatch' }
        }
        New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $after) { throw 'Staged hash mismatch' }
        $rows += [ordered]@{path=$relative; before_sha256=$before; staged_sha256=$after}
        $rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'files.json') -Encoding utf8
    }
}
Copy-Item -LiteralPath (Join-Path $projectRoot 'ProjectSettings') -Destination (Join-Path $backup 'ProjectSettings-before') -Recurse
$manifest = Join-Path $isolated 'Assets/StreamingAssets/HumanVision/R4/manifest.json'
$manifestHash = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
@{project=$projectRoot;output=(Join-Path $build 'humanvision-topdown.apk');manifest_sha256=$manifestHash} | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $projectRoot 'r4-parity-build-request.json') -Encoding utf8
$nativeHash = (Get-FileHash -LiteralPath $latestNative -Algorithm SHA256).Hash.ToLowerInvariant()
@{audit='tools/test/verify_android_native.py';native_sha256=$nativeHash;abi='arm64-v8a';api_level=26;ncnn_vulkan_symbols_verified=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $projectRoot 'android-gpu-bridge-symbols.json') -Encoding utf8
Write-Output "Staged $($rows.Count) changed files; backups and hashes: $backup"
Write-Output "Native SHA256: $nativeHash"
