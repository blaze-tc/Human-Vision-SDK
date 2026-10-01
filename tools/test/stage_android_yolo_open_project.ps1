param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$NativeLibrary,
    [Parameter(Mandatory)][string]$EditorStateFile,
    [Parameter(Mandatory)][string]$BackupDirectory,
    [string]$Project = 'E:/UnityProject/Human-Vision-SDK-Test'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath $Project).Path
if ($projectRoot -ne 'E:\UnityProject\Human-Vision-SDK-Test') { throw 'Adapter restricted to authorized open project' }
$state = Get-Content -LiteralPath $EditorStateFile -Raw | ConvertFrom-Json
if ([IO.Path]::GetFullPath($state.project) -ne $projectRoot -or $state.isPlaying -ne $false -or $state.isCompiling -ne $false) { throw 'Editor project must be idle and not playing' }
$age = ([DateTimeOffset]::UtcNow - [DateTimeOffset]::Parse($state.checked_utc)).TotalSeconds
if ($age -lt 0 -or $age -gt 120) { throw 'Editor state must be captured within 120 seconds' }
if (@($state.scenes).Count -eq 0) { throw 'Saved loaded scene evidence is required' }
foreach ($scene in $state.scenes) { if ($scene.isDirty -ne $false -or !$scene.path -or $scene.isLoaded -ne $true) { throw 'Every loaded scene must be saved and clean' } }
if (Test-Path -LiteralPath $BackupDirectory) { throw 'BackupDirectory must be new' }
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$isolated = Join-Path $build 'UnityProject'
$manifest = Get-Content -LiteralPath (Join-Path $build 'stage-manifest.json') -Raw | ConvertFrom-Json
if ($manifest.size -eq 640 -and ($manifest.shape_id -ne 'rectangle640x384' -or $manifest.input_width -ne 640 -or $manifest.input_height -ne 384 -or $manifest.source_aspect_ratio -ne '16:9')) { throw 'Reviewed rectangle640x384 stage shape/source identity differs' }
$native = (Resolve-Path -LiteralPath $NativeLibrary).Path
$nativeHash = (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash.ToLowerInvariant()
if ($nativeHash -ne $manifest.native_sha256 -or (Get-FileHash -LiteralPath (Join-Path $isolated 'Assets/Plugins/Android/arm64-v8a/libhumanvision.so') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $nativeHash) { throw 'Explicit reviewed native identity differs' }
& py -3 (Join-Path $PSScriptRoot 'stage_android_yolo_eval.py') --runtime (Join-Path $isolated 'Assets/StreamingAssets/HumanVision/Runtime') --size $manifest.size --verify-only
if ($LASTEXITCODE -ne 0) { throw 'Staged runtime selection/hash closure failed' }
$audit = Get-Content -LiteralPath (Join-Path $isolated 'android-gpu-bridge-symbols.json') -Raw | ConvertFrom-Json
if ($audit.native_sha256 -ne $nativeHash -or $audit.ncnn_vulkan_symbols_verified -ne $true) { throw 'Native audit metadata differs' }
foreach ($property in $manifest.selected_files.PSObject.Properties) {
    foreach ($runtimeRoot in @($isolated,(Join-Path $isolated 'Assets/StreamingAssets/HumanVision/Runtime'))) {
        if ((Get-FileHash -LiteralPath (Join-Path $runtimeRoot $property.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $property.Value) { throw 'Runtime differs from reviewed stage manifest' }
    }
}
if ((Get-FileHash -LiteralPath (Join-Path $isolated 'Assets/StreamingAssets/HumanVision/Diagnostic/video-1.mp4') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.video_sha256) { throw 'Staged video hash differs' }
New-Item -ItemType Directory -Path $BackupDirectory | Out-Null
$backup = (Resolve-Path -LiteralPath $BackupDirectory).Path
$rows = @()
foreach ($relativeRoot in @('Assets/HumanVision','Assets/Plugins/Android','Assets/StreamingAssets/HumanVision/Runtime','Assets/StreamingAssets/HumanVision/Diagnostic','profiles','modelpacks')) {
    $from = Join-Path $isolated $relativeRoot
    if (!(Test-Path -LiteralPath $from)) { throw "Missing staged source: $from" }
    foreach ($file in Get-ChildItem -LiteralPath $from -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($isolated,$file.FullName)
        $target = Join-Path $projectRoot $relative
        $before = if (Test-Path -LiteralPath $target) { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
        $after = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($before -ne $after) { $rows += @{path=$relative;before_sha256=$before;staged_sha256=$after;source=$file.FullName} }
    }
}
$request = @{project=$projectRoot;output=(Join-Path $build 'humanvision-topdown.apk');manifest_sha256='';video_sha256=$manifest.video_sha256;frame_index=0} | ConvertTo-Json
$request | Set-Content -LiteralPath (Join-Path $backup 'new-build-request.json') -Encoding utf8
foreach ($pair in @(@('r4-parity-build-request.json',(Join-Path $backup 'new-build-request.json')), @('android-gpu-bridge-symbols.json',(Join-Path $isolated 'android-gpu-bridge-symbols.json')))) {
    $target=Join-Path $projectRoot $pair[0]
    $before=if(Test-Path -LiteralPath $target){(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}
    $rows += @{path=$pair[0];before_sha256=$before;staged_sha256=(Get-FileHash -LiteralPath $pair[1] -Algorithm SHA256).Hash.ToLowerInvariant();source=$pair[1]}
}
# Back up all replacements and settings before the first project mutation.
foreach ($row in $rows) {
    if (!$row.before_sha256) { continue }
    $saved=Join-Path $backup $row.path
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot $row.path) -Destination $saved
    if ((Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.before_sha256) { throw 'Backup hash mismatch' }
}
$settingsRows=@()
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'ProjectSettings') -Recurse -File) {
    $relative=[IO.Path]::GetRelativePath($projectRoot,$file.FullName)
    $saved=Join-Path $backup $relative
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $saved
    $hash=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ((Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'ProjectSettings backup hash mismatch' }
    $settingsRows+=@{path=$relative;sha256=$hash}
}
$settingsRows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'settings.json') -Encoding utf8
$sceneRows=@()
foreach ($name in @('HumanVisionCameraDemo.unity','HumanVisionCameraSettings.unity')) {
    foreach ($suffix in @('', '.meta')) {
        $relative='Assets/HumanVisionR4/Scenes/'+$name+$suffix
        $target=Join-Path $projectRoot $relative
        $hash=$null
        if (Test-Path -LiteralPath $target) {
            $hash=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
            $saved=Join-Path $backup $relative
            New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $saved
            if ((Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'Evaluation scene backup hash mismatch' }
        }
        $sceneRows+=@{path=$relative;before_sha256=$hash}
    }
}
$sceneRows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'scenes.json') -Encoding utf8
$rows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $backup 'files.json') -Encoding utf8
foreach ($row in $rows) {
    $target=Join-Path $projectRoot $row.path
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $row.source -Destination $target -Force
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.staged_sha256) { throw 'Staged hash mismatch' }
}
& py -3 (Join-Path $PSScriptRoot 'stage_android_yolo_eval.py') --runtime (Join-Path $projectRoot 'Assets/StreamingAssets/HumanVision/Runtime') --size $manifest.size --verify-only
if ($LASTEXITCODE -ne 0) { throw 'Open project runtime closure failed' }
Write-Output "Staged $($rows.Count) files; verified backups/ProjectSettings: $backup"
