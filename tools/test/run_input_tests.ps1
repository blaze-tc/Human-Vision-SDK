param(
    [ValidateSet('Core', 'UnitySources', 'Adapter', 'Package')][string]$Phase = 'Core',
    [string]$Output = 'out/input/task1',
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [ValidatePattern('^HumanVision\.Input\.Tests\.FrameContractTests(\.[A-Za-z][A-Za-z0-9]*)?$')]
    [string]$TestFilter = 'HumanVision.Input.Tests.FrameContractTests'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath = [IO.Path]::GetFullPath((Join-Path $repo $Output))
$allowedOutput = [IO.Path]::GetFullPath((Join-Path $repo 'out/input')) + [IO.Path]::DirectorySeparatorChar
if (-not $outputPath.StartsWith($allowedOutput, [StringComparison]::OrdinalIgnoreCase)) { throw 'Output must be inside this checkout out/input directory.' }
if (-not (Test-Path -LiteralPath $Unity -PathType Leaf)) { throw "Unity executable missing: $Unity" }
if ($Phase -ne 'Core') { throw "Phase $Phase is not implemented yet; no tests ran." }
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
$xmlPath = Join-Path $runPath 'results.xml'
$logPath = Join-Path $runPath 'unity.log'
$arguments = "-batchmode -projectPath `"$project`" -runTests -testPlatform EditMode -testFilter $TestFilter -testResults `"$xmlPath`" -logFile `"$logPath`""
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
$process.Refresh()
$exitCode = $process.ExitCode
$status = 'Failed'
$total = 0; $passed = 0; $failed = 0
$testNames = @()
$expectedNames = @('OldGenerationCannotPublish', 'ClosingWaitsForCopyNotInference', 'FrameMetadataUsesActualGeometry', 'InputAssemblyHasNoInferenceReferences', 'EveryOutstandingCopyMustCompleteBeforeResourceRetires', 'ForeignOrRetiredResourceCannotAcquireLease', 'TextureCannotHaveTwoIndependentDestroyOwners', 'PublicationTimelineRejectsInvalidValuesAndPreservesLatest', 'PublicationTimelineContinuesAcrossGenerations', 'TimestampContractDeclaresIndependentClockAndSourceObservation', 'SourceObservationValidityRespectsClockDomain') | ForEach-Object { "HumanVision.Input.Tests.FrameContractTests.$_" }
if ($TestFilter -ne 'HumanVision.Input.Tests.FrameContractTests') { $expectedNames = @($TestFilter) }
if (Test-Path -LiteralPath $xmlPath) {
    [xml]$results = Get-Content -LiteralPath $xmlPath -Raw
    $cases = @($results.SelectNodes('//test-case'))
    $total = $cases.Count
    $passed = @($cases | Where-Object { $_.result -eq 'Passed' }).Count
    $failed = @($cases | Where-Object { $_.result -ne 'Passed' }).Count
    $testNames = @($cases | ForEach-Object { $_.fullname })
    $selectionDifference = @(Compare-Object ($expectedNames | Sort-Object) ($testNames | Sort-Object))
    if ($exitCode -eq 0 -and $total -eq $expectedNames.Count -and $selectionDifference.Count -eq 0 -and $failed -eq 0 -and $results.'test-run'.result -eq 'Passed') { $status = 'Passed' }
}
$artifactHashes = [ordered]@{}
foreach ($artifact in @($xmlPath, $logPath, (Join-Path $project 'Library/ScriptAssemblies/HumanVision.Input.dll'), (Join-Path $project 'Library/ScriptAssemblies/HumanVision.Input.Tests.dll'))) {
    if (Test-Path -LiteralPath $artifact -PathType Leaf) { $artifactHashes[$artifact] = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash }
}
$summary = [ordered]@{ runId = $runId; phase = $Phase; project = $project; unity = $Unity; arguments = $arguments; pid = $process.Id; exitCode = $exitCode; status = $status; total = $total; passed = $passed; failed = $failed; selectedTests = $testNames; xml = $xmlPath; log = $logPath; artifactSha256 = $artifactHashes }
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $runPath 'summary.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 5
if ($status -ne 'Passed') { throw "Unity input tests failed; inspect $logPath and $xmlPath. No stale result was selected." }
