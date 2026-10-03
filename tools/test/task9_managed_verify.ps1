param([ValidateSet('canonical','upm')][string]$Kind='canonical',[string]$SourceRoot='', [string]$Output='out/input/task9-managed')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$source=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$repo}
$project=Join-Path $repo "$Output-$Kind"
if(Test-Path -LiteralPath $project){throw 'Managed fixture is immutable; use a new Output'}
foreach($folder in @('Assets/HumanVision/Runtime','Assets/HumanVision/Demo','Assets/Tests','Assets/Plugins/x86_64','Packages','ProjectSettings')){New-Item -ItemType Directory -Force (Join-Path $project $folder)|Out-Null}
$runtime=if($Kind -eq 'canonical'){"$source/unity/HumanVisionDemo/Assets/HumanVision/Runtime"}else{"$source/upm/com.blazetc.humanvision/Runtime"}
Copy-Item "$runtime/*" "$project/Assets/HumanVision/Runtime" -Recurse
if($Kind -eq 'canonical'){Copy-Item "$source/unity/HumanVisionDemo/Assets/HumanVision/Demo/*" "$project/Assets/HumanVision/Demo" -Recurse}
$test=if($Kind -eq 'canonical'){"$source/unity/HumanVisionDemo/Assets/HumanVision/Tests/EditMode/InputAdapterTests.cs"}else{"$source/upm/com.blazetc.humanvision/Tests/EditMode/InputAdapterTests.cs"}
Copy-Item $test "$project/Assets/Tests/InputAdapterTests.cs"
@{name='HumanVision.Tests.EditMode';references=@('HumanVision.Runtime','HumanVision.Demo','HumanVision.Input');includePlatforms=@('Editor');overrideReferences=$true;precompiledReferences=@('nunit.framework.dll');optionalUnityReferences=@('TestAssemblies');defineConstraints=@('UNITY_INCLUDE_TESTS')}|ConvertTo-Json -Depth 4|Set-Content "$project/Assets/Tests/Test.asmdef"
foreach($library in @('humanvision.dll','humanvision_onnxruntime.dll')){Copy-Item "$repo/out/input-sdk-host/bin/Release/$library" "$project/Assets/Plugins/x86_64/"}
$manifest=Get-Content "$source/unity/HumanVisionDemo/Packages/manifest.json" -Raw|ConvertFrom-Json
$manifest.dependencies|Add-Member -NotePropertyName 'com.blazetc.humanvision.input' -NotePropertyValue ('file:'+((Join-Path $source 'upm/com.blazetc.humanvision.input') -replace '\\','/'))
$manifest|ConvertTo-Json -Depth 4|Set-Content "$project/Packages/manifest.json"
'm_EditorVersion: 2021.3.45f1'|Set-Content "$project/ProjectSettings/ProjectVersion.txt"
$unity='D:/Developer/2021.3.45f1/Editor/Unity.exe'
$arguments="-batchmode -force-d3d11 -projectPath `"$project`" -runTests -testPlatform EditMode -testFilter HumanVision.Tests.InputAdapterTests -testResults `"$project/tests.xml`" -logFile `"$project/unity.log`""
$process=Start-Process $unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit();$process.Refresh()
[xml]$result=Get-Content "$project/tests.xml"
@{kind=$Kind;source=$source;project=$project;exitCode=$process.ExitCode;passed=$result.'test-run'.passed;failed=$result.'test-run'.failed}|ConvertTo-Json|Set-Content "$project/receipt.json"
if($process.ExitCode -ne 0 -or $result.'test-run'.failed -ne '0' -or $result.'test-run'.passed -ne '8'){throw 'Actual source-specific adapter tests did not pass8/8'}
Write-Output "$Kind adapter PASS8/8"
