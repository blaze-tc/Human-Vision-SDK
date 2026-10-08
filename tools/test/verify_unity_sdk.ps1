param(
    [string]$Filter = 'HumanVision.Tests.HumanVisionConfigTests',
    [switch]$CompileOnly,
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [string]$Payload = 'E:/Project/Human Vision SDK/.worktrees/android-ncnn-vulkan/out/input/production-correction/quality-q4-package-20261005-v2'
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$unityData = Join-Path (Split-Path $Unity) 'Data'
$output = Join-Path $repo 'out/sdk-api-verification'
New-Item -ItemType Directory -Force $output | Out-Null
$references = @(Get-ChildItem "$unityData/NetStandard/ref/2.1.0" -Filter '*.dll';
    Get-ChildItem "$unityData/NetStandard/compat/2.1.0/shims/netfx" -Filter '*.dll';
    Get-ChildItem "$unityData/Managed/UnityEngine" -Filter '*.dll')
$referenceArgs = @($references | ForEach-Object { '/reference:"' + $_.FullName + '"' })
$referenceArgs += '/reference:"E:/UnityProject/Human-Vision-SDK-Test/Library/ScriptAssemblies/UnityEngine.UI.dll"'
foreach ($platform in @('Editor','Android')) {
    $platformOut = Join-Path $output $platform
    New-Item -ItemType Directory -Force $platformOut | Out-Null
    foreach ($part in @('Input','Runtime','Demo','Editor')) {
        if ($platform -eq 'Android' -and $part -eq 'Editor') { continue }
        $arguments = @('/nologo','/target:library','/unsafe','/langversion:9','/nostdlib+','/nowarn:0649') + $referenceArgs
        $arguments += '/out:"' + $platformOut + '/HumanVision.' + $part + '.dll"'
        $arguments += if ($platform -eq 'Android') { '/define:UNITY_ANDROID' } else { '/define:UNITY_EDITOR' }
        if ($part -ne 'Input') { $arguments += '/reference:"' + $platformOut + '/HumanVision.Input.dll"' }
        if ($part -in @('Demo','Editor')) { $arguments += '/reference:"' + $platformOut + '/HumanVision.Runtime.dll"' }
        if ($part -eq 'Editor' -or $platform -eq 'Editor') {
            $arguments += Get-ChildItem "$unityData/Managed" -Filter 'UnityEditor*.dll' | Where-Object Name -ne 'UnityEditor.dll' | ForEach-Object { '/reference:"' + $_.FullName + '"' }
        }
        if ($part -eq 'Editor') { $arguments += '/reference:"' + $platformOut + '/HumanVision.Demo.dll"' }
        $source = if ($part -eq 'Input') { "$repo/upm/com.blazetc.humanvision.input/Runtime" } else { "$repo/unity/HumanVisionDemo/Assets/HumanVision/$part" }
        $arguments += Get-ChildItem $source -Recurse -Filter '*.cs' | ForEach-Object { '"' + $_.FullName + '"' }
        $response = Join-Path $platformOut "$part.rsp"
        $arguments | Set-Content -LiteralPath $response
        & "$unityData/MonoBleedingEdge/bin/mono.exe" "$unityData/MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe" "@$response"
        if ($LASTEXITCODE -ne 0) { throw "$platform $part compilation failed" }
    }
}
Write-Output 'SDK Input/Runtime/Demo/Editor and Android managed compile PASS.'
if ($CompileOnly) { return }
$project = Join-Path $output 'project'
foreach ($folder in @('Assets/HumanVision','Packages','ProjectSettings')) {
    New-Item -ItemType Directory -Force (Join-Path $project $folder) | Out-Null
}
foreach ($part in @('Runtime','Demo','Editor','Tests')) {
    Copy-Item -LiteralPath "$repo/unity/HumanVisionDemo/Assets/HumanVision/$part" -Destination "$project/Assets/HumanVision" -Recurse -Force
}
$manifest = Get-Content "$repo/unity/HumanVisionDemo/Packages/manifest.json" -Raw | ConvertFrom-Json
$manifest.dependencies | Add-Member -NotePropertyName 'com.blazetc.humanvision.input' -NotePropertyValue ('file:' + ("$repo/upm/com.blazetc.humanvision.input" -replace '\\','/')) -Force
$manifest.dependencies | Add-Member -NotePropertyName 'com.unity.modules.screencapture' -NotePropertyValue '1.0.0' -Force
$manifest | ConvertTo-Json -Depth 10 | Set-Content "$project/Packages/manifest.json"
Copy-Item -LiteralPath "$repo/unity/HumanVisionDemo/ProjectSettings/ProjectVersion.txt" -Destination "$project/ProjectSettings/ProjectVersion.txt" -Force
if (Test-Path "$Payload/com.blazetc.humanvision/Runtime/Plugins") {
    Copy-Item -LiteralPath "$Payload/com.blazetc.humanvision/Runtime/Plugins" -Destination "$project/Assets" -Recurse -Force
    New-Item -ItemType Directory -Force "$project/Assets/StreamingAssets/HumanVision" | Out-Null
    Copy-Item -LiteralPath "$Payload/com.blazetc.humanvision/RuntimeData" -Destination "$project/Assets/StreamingAssets/HumanVision/Runtime" -Recurse -Force
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$resultPath = Join-Path $output "$stamp-results.xml"
$logPath = Join-Path $output "$stamp-unity.log"
$env:HV_TEST_RUNTIME_ROOT = $repo
$env:__COMPAT_LAYER = 'RunAsInvoker'
$arguments = "-batchmode -nographics -projectPath `"$project`" -runTests -testPlatform EditMode -testFilter `"$Filter`" -testResults `"$resultPath`" -logFile `"$logPath`""
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if (!(Test-Path -LiteralPath $resultPath)) {
    Get-Content -LiteralPath $logPath -Tail 50
    throw "Unity produced no test result: $logPath"
}
[xml]$result = Get-Content -LiteralPath $resultPath
$summary = $result.'test-run'
Write-Output "Unity tests: $($summary.passed)/$($summary.total) passed; $($summary.failed) failed; $($summary.skipped) skipped. Results: $resultPath"
if ([int]$summary.failed -ne 0 -or [int]$summary.total -eq 0 -or $process.ExitCode -ne 0) {
    $result.SelectNodes('//test-case[failure]') | ForEach-Object { Write-Output "$($_.fullname): $($_.failure.message)" }
    throw "Unity tests failed. See $resultPath and $logPath"
}
