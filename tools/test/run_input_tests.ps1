param(
    [ValidateSet('Core', 'UnitySources', 'Adapter', 'Package')][string]$Phase = 'Core',
    [string]$Output = 'out/input/task1',
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [ValidateSet('Direct3D11','Direct3D12','Vulkan')][string]$GraphicsApi = 'Direct3D11',
    [ValidatePattern('^HumanVision\.Input\.Tests\.(FrameContractTests|UnitySourceTests|UnitySourceLifecycleTests)(\.[A-Za-z][A-Za-z0-9]*)?$')]
    [string]$TestFilter = 'HumanVision.Input.Tests.FrameContractTests'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$allowedOutput = [IO.Path]::GetFullPath((Join-Path $repo 'out/input')) + [IO.Path]::DirectorySeparatorChar
if (-not $outputPath.StartsWith($allowedOutput, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside this checkout out/input directory.' }
if (-not (Test-Path -LiteralPath $Unity -PathType Leaf)) { throw "Unity executable missing: $Unity" }
if($Phase -eq 'Adapter') {
    & "$PSScriptRoot/task9_managed_verify.ps1" -Kind canonical -Output "$Output-adapter"
    & "$PSScriptRoot/task9_managed_verify.ps1" -Kind upm -Output "$Output-adapter"
    return
}
if ($Phase -notin @('Core','UnitySources')) { throw "Phase $Phase is not implemented yet; no tests ran." }
$project = Join-Path $outputPath 'project'
foreach ($directory in @($project, (Join-Path $project 'Assets'), (Join-Path $project 'Packages'), (Join-Path $project 'ProjectSettings'))) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$runId = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffffffZ') + '-' + [Guid]::NewGuid().ToString('N')
$runPath = Join-Path $outputPath $runId
New-Item -ItemType Directory -Path $runPath | Out-Null
# Reuse pinned test framework dependencies from an existing cache, read-only.
$dependencies = @{ 'com.blazetc.humanvision.input' = ('file:' + ((Join-Path $repo 'upm/com.blazetc.humanvision.input') -replace '\\','/')) }
$cacheRoots = @((Join-Path $repo 'unity/HumanVisionDemo/Library/PackageCache'), 'E:/UnityProject/Human-Vision-SDK-Test/Library/PackageCache')
foreach ($package in @('com.unity.test-framework@1.1.33', 'com.unity.ext.nunit@1.0.6')) {
    $cached = $cacheRoots | ForEach-Object { Join-Path $_ $package } | Where-Object { Test-Path (Join-Path $_ 'package.json') } | Select-Object -First 1
    $name, $version = $package -split '@'
    $dependencies[$name] = if ($cached) { 'file:' + ($cached -replace '\\','/') } else { $version }
}
@{ dependencies = $dependencies; testables = @('com.blazetc.humanvision.input') } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $project 'Packages/manifest.json') -Encoding utf8
"m_EditorVersion: 2021.3.45f1`nm_EditorVersionWithRevision: 2021.3.45f1 (0da89fac8e79)" | Set-Content -LiteralPath (Join-Path $project 'ProjectSettings/ProjectVersion.txt') -Encoding utf8

$coreNames = @('AndroidInitializationFailureRemainsActionable', 'OldGenerationCannotPublish', 'ClosingWaitsForCopyNotInference', 'FrameMetadataUsesActualGeometry', 'InputAssemblyHasNoInferenceReferences', 'EveryOutstandingCopyMustCompleteBeforeResourceRetires', 'ForeignOrRetiredResourceCannotAcquireLease', 'TextureCannotHaveTwoIndependentDestroyOwners', 'PublicationTimelineRejectsInvalidValuesAndPreservesLatest', 'PublicationTimelineContinuesAcrossGenerations', 'TimestampContractDeclaresIndependentClockAndSourceObservation', 'SourceObservationValidityRespectsClockDomain', 'CopyLeaseReuseWaitsForExactRetirementAcknowledgment') | ForEach-Object { "HumanVision.Input.Tests.FrameContractTests.$_" }
    $selections = @(@{ platform='EditMode'; filter=$TestFilter; expected=$(if($TestFilter -eq 'HumanVision.Input.Tests.FrameContractTests'){$coreNames}else{@($TestFilter)}) })
if ($Phase -eq 'UnitySources') {
    $fixture = 'E:/Project/Human Vision SDK/video-1.mp4'
    if ((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash -ne 'E3620101D8218E7E9F2736CF5DAB7A497BFCFC23E33A40244B63AE317C1BB0C8') { throw 'Approved MP4 fixture hash mismatch.' }
    $selections = @(
        @{platform='EditMode';filter='HumanVision.Input.Tests.UnitySourceTests';expected=@('AsymmetricMarkerTransformsExactlyOnce','RequestedResolutionIsNotActualResolution','NormalizerRejectsWorkerThread','MidtoneEncodingMatchesGpuPixels','NormalizerPreservesCallerActiveTarget','DisposeClearsOnlyOwnedActiveTarget') | ForEach-Object {"HumanVision.Input.Tests.UnitySourceTests.$_"}},
        @{platform='PlayMode';filter='HumanVision.Input.Tests.UnitySourceLifecycleTests';expected=@('PreviewWithoutModels','SwitchAndPauseRejectLateFrames','PreviewHasNoPerFrameManagedAllocationsAfterWarmup','GeometryChangeRetiresOutstandingCopy','PublicationHasNoPerFrameManagedAllocationsAfterWarmup','UnregisteredOutputIsDestroyedOnClose','NoConsumerCloseCleansImmediateErrorAndRepeatedReopen','LateErrorAfterPauseCannotStopSource','LateCallbacksAfterRawDestroyAreIgnored','SameTurnUnityCopyAfterClosePreservesPixels','SameTurnVideoPublicationCopySurvivesClose','PublicationEncodingMatchesTextureFlag') | ForEach-Object {"HumanVision.Input.Tests.UnitySourceLifecycleTests.$_"}}
    )
}
$receipts = @()
foreach ($selection in $selections) {
    $platform = $selection.platform
    $xmlPath = Join-Path $runPath "$platform-results.xml"
    $logPath = Join-Path $runPath "$platform-unity.log"
    $graphicsArgument = switch ($GraphicsApi) {Direct3D11 {"-force-d3d11"}; Direct3D12 {"-force-d3d12"}; Vulkan {"-force-vulkan"}}
    $arguments = "-batchmode $graphicsArgument -projectPath `"$project`" -runTests -testPlatform $platform -testFilter $($selection.filter) -testResults `"$xmlPath`" -logFile `"$logPath`""
    $startInfo = [Diagnostics.ProcessStartInfo]::new($Unity, $arguments)
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    # Override only this child process's compatibility environment; no registry/UAC change.
    $startInfo.Environment['__COMPAT_LAYER'] = 'RunAsInvoker'
    $process = [Diagnostics.Process]::Start($startInfo)
    $timedOut = -not $process.WaitForExit(240000)
    if ($timedOut) { $process.Kill(); $process.WaitForExit() }
    $process.Refresh()
    $exitCode = $process.ExitCode
    $status='Failed';$total=0;$passed=0;$failed=0;$testNames=@()
    if (Test-Path -LiteralPath $xmlPath) {
        [xml]$results=Get-Content -LiteralPath $xmlPath -Raw
        $cases=@($results.SelectNodes('//test-case'));$total=$cases.Count
        $passed=@($cases | Where-Object {$_.result -eq 'Passed'}).Count
        $failed=@($cases | Where-Object {$_.result -ne 'Passed'}).Count
        $testNames=@($cases | ForEach-Object {$_.fullname})
        $selectionDifference=@(Compare-Object ($selection.expected | Sort-Object) ($testNames | Sort-Object))
        if (-not $timedOut -and $exitCode -eq 0 -and $total -eq $selection.expected.Count -and $selectionDifference.Count -eq 0 -and $failed -eq 0 -and $results.'test-run'.result -eq 'Passed') {$status='Passed'}
    }
    $hashes=[ordered]@{}
    foreach ($artifact in @($xmlPath,$logPath,(Join-Path $project 'Library/ScriptAssemblies/HumanVision.Input.dll'),(Join-Path $project 'Library/ScriptAssemblies/HumanVision.Input.Tests.dll'),(Join-Path $project 'Library/ScriptAssemblies/HumanVision.Input.PlayMode.Tests.dll'))) {
        if(Test-Path -LiteralPath $artifact -PathType Leaf){$hashes[$artifact]=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash}
    }
    $receipt=[ordered]@{runId=$runId;phase=$Phase;platform=$platform;project=$project;unity=$Unity;compatibility='per-process RunAsInvoker';arguments=$arguments;pid=$process.Id;exitCode=$exitCode;timedOut=$timedOut;status=$status;total=$total;passed=$passed;failed=$failed;selectedTests=$testNames;xml=$xmlPath;log=$logPath;artifactSha256=$hashes}
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runPath "$platform-summary.json") -Encoding utf8
    $receipts += $receipt
    $receipt | ConvertTo-Json -Depth 6
    if ($status -ne 'Passed') {throw "Unity $platform input tests failed; inspect $logPath and $xmlPath. Zero/missing/stale/mis-selected tests never pass."}
}
$sourceHashes=[ordered]@{}
Get-ChildItem -LiteralPath (Join-Path $repo 'upm/com.blazetc.humanvision.input') -Recurse -File | Sort-Object FullName | ForEach-Object {$sourceHashes[$_.FullName]=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
$sourceHashes[(Join-Path $repo 'tools/test/run_input_tests.ps1')]=(Get-FileHash -LiteralPath (Join-Path $repo 'tools/test/run_input_tests.ps1') -Algorithm SHA256).Hash
@{runId=$runId;phase=$Phase;status='Passed';runs=$receipts;sourceSha256=$sourceHashes} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runPath 'summary.json') -Encoding utf8
