param([string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$testProject = Join-Path $taskRoot 'out/upm040-verification'
New-Item "$testProject/Assets/Editor", "$testProject/Packages" -ItemType Directory -Force | Out-Null
$sourcePackage = Join-Path $taskRoot 'out/releases/0.4.0-preview.1/com.blazetc.humanvision-0.4.0-preview.1.tgz'
$hash = (Get-FileHash -LiteralPath $sourcePackage -Algorithm SHA256).Hash.ToLowerInvariant()
$inputFolder = Join-Path $taskRoot 'out/upm040-inputs'
New-Item $inputFolder -ItemType Directory -Force | Out-Null
$package = (Join-Path $inputFolder "$hash.tgz").Replace('\','/')
Copy-Item -LiteralPath $sourcePackage -Destination $package -Force
@{dependencies=@{'com.blazetc.humanvision'="file:$package"}} | ConvertTo-Json -Depth 4 | Set-Content "$testProject/Packages/manifest.json" -Encoding utf8
Copy-Item -LiteralPath "$PSScriptRoot/Upm040ImportCheck.cs" -Destination "$testProject/Assets/Editor/Upm040ImportCheck.cs" -Force
$arguments = "-batchmode -nographics -projectPath `"$testProject`" -executeMethod Upm040ImportCheck.Run -logFile `"$taskRoot/out/upm040-import.log`""
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "UPM import failed ($($process.ExitCode)); see out/upm040-import.log" }
if (!(Test-Path "$testProject/upm-import-pass.txt")) { throw 'Missing UPM import success evidence' }
if ((Get-Item "$testProject/upm-import-pass.txt").LastWriteTimeUtc -lt $startedAt) { throw 'Stale UPM import evidence' }
Get-Content "$testProject/upm-import-pass.txt"
