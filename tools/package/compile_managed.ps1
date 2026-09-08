param(
    [string]$UnityData = 'D:/Developer/2021.3.45f1/Editor/Data',
    [string]$UiAssembly = 'E:/UnityProject/Human-Vision-SDK-Test/Library/ScriptAssemblies/UnityEngine.UI.dll'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$output = New-Item "$root/out/live-managed-build" -ItemType Directory -Force
$references = @(Get-ChildItem "$UnityData/NetStandard/ref/2.1.0" -Filter '*.dll';
    Get-ChildItem "$UnityData/NetStandard/compat/2.1.0/shims/netfx" -Filter '*.dll';
    Get-ChildItem "$UnityData/Managed/UnityEngine" -Filter '*.dll')
$baseArgs = @('/nologo','/target:library','/unsafe','/langversion:9','/nostdlib+')
$refArgs = @($references | ForEach-Object { '/reference:"' + $_.FullName + '"' })
$refArgs += '/reference:"' + $UiAssembly + '"'
foreach ($part in @('Runtime','Demo','Editor')) {
    $arguments = $baseArgs + $refArgs + ('/out:"' + $output.FullName + '/HumanVision.' + $part + '.dll"')
    if ($part -ne 'Runtime') { $arguments += '/reference:"' + $output.FullName + '/HumanVision.Runtime.dll"' }
    if ($part -eq 'Editor') {
        $arguments += '/reference:"' + $output.FullName + '/HumanVision.Demo.dll"'
        $arguments += '/define:UNITY_EDITOR'
        $arguments += Get-ChildItem "$UnityData/Managed" -Filter 'UnityEditor*.dll' | Where-Object Name -ne 'UnityEditor.dll' | ForEach-Object { '/reference:"' + $_.FullName + '"' }
    }
    $arguments += Get-ChildItem "$root/unity/HumanVisionDemo/Assets/HumanVision/$part" -Recurse -Filter '*.cs' | ForEach-Object { '"' + $_.FullName + '"' }
    $response = Join-Path $output.FullName "$part.rsp"
    $arguments | Set-Content $response
    & "$UnityData/MonoBleedingEdge/bin/mono.exe" "$UnityData/MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe" "@$response"
    if ($LASTEXITCODE -ne 0) { throw "$part assembly compilation failed" }
}
$androidOutput = New-Item (Join-Path $output.FullName 'Android') -ItemType Directory -Force
$androidArgs = Get-Content (Join-Path $output.FullName 'Demo.rsp') | Where-Object { $_ -notlike '/out:*' }
$androidArgs += '/define:UNITY_ANDROID'
$androidArgs += '/out:"' + $androidOutput.FullName + '/HumanVision.Demo.dll"'
$androidResponse = Join-Path $output.FullName 'DemoAndroid.rsp'
$androidArgs | Set-Content $androidResponse
& "$UnityData/MonoBleedingEdge/bin/mono.exe" "$UnityData/MonoBleedingEdge/lib/mono/msbuild/Current/bin/Roslyn/csc.exe" "@$androidResponse"
if ($LASTEXITCODE -ne 0) { throw 'Android conditional managed compilation failed' }
Write-Output 'Managed assemblies and Android conditional code compiled. No Unity runtime or test execution.'
