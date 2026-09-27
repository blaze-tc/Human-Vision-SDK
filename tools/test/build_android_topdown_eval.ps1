param(
    [ValidateRange(2,6)][int]$Interval = 4,
    [ValidateRange(1,8)][int]$Capacity = 1,
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [switch]$DiagnosticTrace,
    [string]$VideoDiagnosticPath = '',
    [string]$R4ParityManifest = '',
    [string]$RunLabel = '',
    [switch]$StageOnly,
    [switch]$ResolveConfigurationOnly
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$videoDiagnostic = ![string]::IsNullOrWhiteSpace($VideoDiagnosticPath)
$parity = ![string]::IsNullOrWhiteSpace($R4ParityManifest)
if ($parity -and ($videoDiagnostic -or $DiagnosticTrace)) { throw 'R4 static parity is exclusive with video and sparse diagnostics' }
if ($StageOnly -and !$parity) { throw 'StageOnly is restricted to the authorized R4 parity editor workflow' }
if ($RunLabel -and $RunLabel -notmatch '^[a-zA-Z0-9-]+$') { throw 'RunLabel must contain only letters, digits, or hyphens' }
if (!$parity -and $Capacity -gt 2) { throw 'Live/video acceptance capacity is restricted to 1 or 2' }
$expectedPeople=0
if ($parity) {
    $manifestPath=(Resolve-Path -LiteralPath $R4ParityManifest).Path
    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    $fixtures=if($manifest.cases){@($manifest.cases)}else{@($manifest)}
    foreach($fixture in $fixtures) { $expectedPeople=[Math]::Max($expectedPeople,@($fixture.annotations).Where({$null -ne $_}).Count) }
    if($expectedPeople -gt 8){throw 'Parity fixture exceeds supported body capacity8'}
    if(!$PSBoundParameters.ContainsKey('Capacity')){$Capacity=[Math]::Max(1,$expectedPeople)}
    if($Capacity -lt $expectedPeople){throw 'Parity capacity cannot omit annotated people'}
}
$configuration=@{interval=$Interval;effective_capacity=$Capacity;annotated_people=$expectedPeople;parity=$parity}
if($ResolveConfigurationOnly){$configuration|ConvertTo-Json;return}
$suffix = if ($videoDiagnostic) { '-video-diagnostic' } else { '' }
if ($parity) { $suffix = '-r4-parity' }
if ($RunLabel) { $suffix += "-$RunLabel" }
$output = Join-Path $root "out/android-topdown-eval/interval-$Interval-capacity-$Capacity$suffix"
$stage = Join-Path $output 'stage'
if (Test-Path -LiteralPath $stage) { throw "Stage already exists; preserve or explicitly move it: $stage" }
$python = Join-Path $root '.venv-reference/Scripts/python.exe'
$pack = Join-Path $root 'out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16'
& $python -m tools.test.prepare_android_topdown_eval --pack $pack --profile (Join-Path $root 'profiles/android-ncnn-vulkan.json') --output $stage --interval $Interval
if ($LASTEXITCODE -ne 0) { throw 'Local pack/profile regeneration failed' }
& pwsh -NoProfile -File (Join-Path $root 'tools/package/build_live_native.ps1') -Platform Android -AndroidApiLevel 26 -TopDownEvalTrace:$DiagnosticTrace -R4Parity:$parity
if ($LASTEXITCODE -ne 0) { throw 'Production Android native build failed' }
& $python (Join-Path $root 'tools/test/verify_android_native.py')
if ($LASTEXITCODE -ne 0) { throw 'Production Android ELF/symbol audit failed' }
$project = Join-Path $output 'UnityProject'
New-Item -ItemType Directory -Path $project -Force | Out-Null
$source = Join-Path $root 'unity/HumanVisionDemo'
foreach ($folder in @('Assets/HumanVision','Assets/Scenes','Assets/StreamingAssets','ProjectSettings','Packages')) {
    $from = Join-Path $source $folder
    $to = Join-Path $project $folder
    New-Item -ItemType Directory -Path $to -Force | Out-Null
    if (Test-Path -LiteralPath $from) { Copy-Item -Path (Join-Path $from '*') -Destination $to -Recurse -Force }
}
$editor = Join-Path $project 'Assets/HumanVision/Editor'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'TopDownEvalBuild.cs') -Destination $editor -Force
$probe = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'TopDownEvalProbe.cs') -Raw).Replace('public const int Capacity = 4;', "public const int Capacity = $Capacity;").Replace('public const int Interval = 4;', "public const int Interval = $Interval;")
$probe | Set-Content -LiteralPath (Join-Path $project 'Assets/HumanVision/Demo/Live/TopDownEvalProbe.cs') -Encoding utf8
$videoSource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'TopDownEvalVideoSource.cs') -Raw
if ($videoDiagnostic) {
    $sourceVideo = (Resolve-Path -LiteralPath $VideoDiagnosticPath).Path
    if ([IO.Path]::GetExtension($sourceVideo).ToLowerInvariant() -ne '.mp4') { throw 'Video diagnostic requires an MP4' }
    $videoName = [IO.Path]::GetFileName($sourceVideo)
    $videoHash = (Get-FileHash -LiteralPath $sourceVideo -Algorithm SHA256).Hash.ToLowerInvariant()
    $videoTarget = Join-Path $project 'Assets/StreamingAssets/HumanVision/Diagnostic'
    New-Item -ItemType Directory -Path $videoTarget -Force | Out-Null
    Copy-Item -LiteralPath $sourceVideo -Destination (Join-Path $videoTarget $videoName) -Force
    $videoSource = $videoSource.Replace('video-2.mp4', $videoName).Replace('REPLACE_VIDEO_SHA256', $videoHash)
}
$videoSource | Set-Content -LiteralPath (Join-Path $project 'Assets/HumanVision/Demo/Live/TopDownEvalVideoSource.cs') -Encoding utf8
$paritySource = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'TopDownEvalParitySource.cs') -Raw
if ($parity) {
    $manifestPath = (Resolve-Path -LiteralPath $R4ParityManifest).Path
    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $parityTarget = Join-Path $project 'Assets/StreamingAssets/HumanVision/R4'
    New-Item -ItemType Directory -Path $parityTarget -Force | Out-Null
    Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $parityTarget 'manifest.json')
    $fixtures=if($manifest.cases) { @($manifest.cases) } else { @($manifest) }
    foreach($fixture in $fixtures) {
      $folder=if($manifest.cases) { $fixture.folder } else { '' }
      if($folder -and [IO.Path]::GetFileName($folder) -ne $folder) { throw 'R4 fixture folder invalid' }
      $artifactTarget=Join-Path $parityTarget $folder
      New-Item -ItemType Directory -Path $artifactTarget -Force | Out-Null
      foreach ($property in $fixture.artifacts.PSObject.Properties) {
        $artifact = $property.Value
        if ([IO.Path]::GetFileName($artifact.file) -ne $artifact.file) { throw 'R4 artifact filename is invalid' }
        $path = Join-Path (Join-Path (Split-Path $manifestPath) $folder) $artifact.file
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $artifact.sha256) { throw 'R4 artifact hash mismatch' }
        Copy-Item -LiteralPath $path -Destination (Join-Path $artifactTarget $artifact.file)
      }
    }
    $paritySource = $paritySource.Replace('REPLACE_MANIFEST_SHA256', $manifestHash)
}
$paritySource | Set-Content -LiteralPath (Join-Path $project 'Assets/HumanVision/Demo/Live/TopDownEvalParitySource.cs') -Encoding utf8
$permissionSource = Join-Path $root 'upm/com.blazetc.humanvision/Runtime/Plugins/Android/HumanVisionPermissions.androidlib'
$permissionTarget = Join-Path $project 'Assets/Plugins/Android/HumanVisionPermissions.androidlib'
New-Item -ItemType Directory -Path $permissionTarget -Force | Out-Null
Copy-Item -Path (Join-Path $permissionSource '*') -Destination $permissionTarget -Recurse -Force
foreach ($folder in @('profiles','modelpacks')) {
    Copy-Item -Path (Join-Path $stage $folder) -Destination $project -Recurse -Force
}
$runtime = Join-Path $project 'Assets/StreamingAssets/HumanVision/Runtime'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
$entries = @()
foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
    $relative = [IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\','/')
    $destination = Join-Path $runtime $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    $entries += @{ path = $relative; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
@{version='topdown-local-eval';files=$entries} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runtime 'index.json') -Encoding utf8
$plugins = Join-Path $project 'Assets/Plugins/Android/arm64-v8a'
& pwsh -NoProfile -File (Join-Path $root 'tools/test/verify_android_gpu_bridge_gate_libs.ps1') -Mode Stage -PluginDirectory $plugins
if ($LASTEXITCODE -ne 0) { throw 'Native dependency stage failed' }
$nativeHash = (Get-FileHash -LiteralPath (Join-Path $plugins 'libhumanvision.so') -Algorithm SHA256).Hash.ToLowerInvariant()
@{audit='tools/test/verify_android_native.py';native_sha256=$nativeHash;abi='arm64-v8a';api_level=26;ncnn_vulkan_symbols_verified=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project 'android-gpu-bridge-symbols.json') -Encoding utf8
$apk = Join-Path $output 'humanvision-topdown.apk'
$configuration|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $output 'configuration.json') -Encoding utf8
if($StageOnly) {
    if(!$parity){throw 'StageOnly is restricted to the authorized R4 parity editor workflow'}
    Write-Output "R4 staged evaluation project: $project"
    return
}
$log = Join-Path $output 'unity-build.log'
$env:HV_TOPDOWN_EVAL = '1'
$env:HV_TOPDOWN_APK = $apk
if ($videoDiagnostic) { $env:HV_TOPDOWN_VIDEO = $videoHash }
try {
    $videoFlag = if ($videoDiagnostic) { '-humanvisionTopDownVideo ' } else { '' }
    if ($parity) { $videoFlag = '-humanvisionR4Parity ' }
    $arguments = "-batchmode -nographics -humanvisionTopDownEval $videoFlag-projectPath `"$project`" -executeMethod HumanVision.Editor.TopDownEvalBuild.Build -logFile `"$log`" -quit"
    $process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Unity build failed ($($process.ExitCode)); see $log" }
} finally {
    Remove-Item Env:HV_TOPDOWN_EVAL -ErrorAction SilentlyContinue
    Remove-Item Env:HV_TOPDOWN_APK -ErrorAction SilentlyContinue
    Remove-Item Env:HV_TOPDOWN_VIDEO -ErrorAction SilentlyContinue
}
if (!(Test-Path -LiteralPath $apk)) { throw "APK missing: $apk" }
& pwsh -NoProfile -File (Join-Path $root 'tools/test/verify_android_gpu_bridge_gate_libs.ps1') -Mode Verify -ApkPath $apk
if ($LASTEXITCODE -ne 0) { throw 'APK library closure failed' }
$hashes = [ordered]@{
    interval = $Interval
    capacity = $Capacity
    apk_sha256 = (Get-FileHash -LiteralPath $apk -Algorithm SHA256).Hash.ToLowerInvariant()
    profile_sha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'profiles/android-ncnn-vulkan.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    modelpack_sha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'modelpacks/precision-t-26-ncnn-fp16/modelpack.json') -Algorithm SHA256).Hash.ToLowerInvariant()
    detector_sha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'modelpacks/precision-t-26-ncnn-fp16/detector/model.bin') -Algorithm SHA256).Hash.ToLowerInvariant()
    body_sha256 = (Get-FileHash -LiteralPath (Join-Path $stage 'modelpacks/precision-t-26-ncnn-fp16/body/model.bin') -Algorithm SHA256).Hash.ToLowerInvariant()
    libhumanvision_sha256 = (Get-FileHash -LiteralPath (Join-Path $plugins 'libhumanvision.so') -Algorithm SHA256).Hash.ToLowerInvariant()
}
if ($videoDiagnostic) { $hashes.video_sha256 = $videoHash; $hashes.video_name = $videoName }
if ($parity) { $hashes.parity_manifest_sha256 = $manifestHash; $hashes.parity_route = 'static_gpu_upload' }
$hashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'hashes.json') -Encoding utf8
Write-Output ($hashes | ConvertTo-Json)
