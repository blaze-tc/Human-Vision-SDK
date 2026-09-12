param([string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$testProject = Join-Path $taskRoot 'out/unity040-verification'
New-Item $testProject -ItemType Directory -Force | Out-Null
foreach ($folder in @('Assets/HumanVision','Assets/Scenes','ProjectSettings','Packages')) {
    $destination = Join-Path $testProject $folder
    New-Item $destination -ItemType Directory -Force | Out-Null
    Copy-Item -Path "$taskRoot/unity/HumanVisionDemo/$folder/*" -Destination $destination -Recurse -Force
}
$plugins = Join-Path $testProject 'Assets/Plugins/x86_64'
New-Item $plugins -ItemType Directory -Force | Out-Null
foreach ($name in @('humanvision.dll','humanvision_onnxruntime.dll','hv_dml.dll')) {
    Copy-Item -LiteralPath "$taskRoot/build/windows-live/bin/Release/$name" -Destination $plugins -Force
}
foreach ($name in @('avformat-61.dll','avcodec-61.dll','avutil-59.dll','swscale-8.dll','swresample-5.dll')) {
    Copy-Item -LiteralPath "$taskRoot/out/live-deps/ffmpeg-windows/$name" -Destination $plugins -Force
}
$media = Join-Path $testProject 'Assets/StreamingAssets'
New-Item $media -ItemType Directory -Force | Out-Null
Copy-Item -Path "$taskRoot/unity/HumanVisionDemo/Assets/StreamingAssets/*" -Destination $media -Recurse -Force
$env:HV_TEST_RUNTIME_ROOT = $taskRoot
Set-Content -LiteralPath (Join-Path $testProject 'runtime-root.txt') -Value $taskRoot -Encoding utf8
$arguments = "-batchmode -nographics -projectPath `"$testProject`" -runTests -testPlatform EditMode -testResults `"$taskRoot/out/unity040-tests.xml`" -logFile `"$taskRoot/out/unity040-tests.log`""
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "Unity tests failed ($($process.ExitCode)); see out/unity040-tests.log" }
if ((Get-Item "$taskRoot/out/unity040-tests.xml").LastWriteTimeUtc -lt $startedAt) { throw 'Stale Unity test results' }
[xml]$results = Get-Content "$taskRoot/out/unity040-tests.xml"
if ($results.'test-run'.result -ne 'Passed') { throw 'Unity test results did not pass' }
Write-Output "Unity EditMode tests: $($results.'test-run'.passed) passed / $($results.'test-run'.total) total."
