param([string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe', [string]$GitRevision = '')
$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$testProject = Join-Path $taskRoot 'out/upm040-verification'
New-Item "$testProject/Assets/Editor", "$testProject/Packages" -ItemType Directory -Force | Out-Null
$sourcePackage = Join-Path $taskRoot 'out/releases/0.4.0-preview.2/com.blazetc.humanvision-0.4.0-preview.2.tgz'
$hash = (Get-FileHash -LiteralPath $sourcePackage -Algorithm SHA256).Hash.ToLowerInvariant()
$inputFolder = Join-Path $taskRoot 'out/upm040-inputs'
New-Item $inputFolder -ItemType Directory -Force | Out-Null
$package = (Join-Path $inputFolder "$hash.tgz").Replace('\','/')
Copy-Item -LiteralPath $sourcePackage -Destination $package -Force
if ($GitRevision -and $GitRevision -notmatch '^[0-9a-f]{40}$') { throw 'GitRevision must be an immutable full commit hash' }
$dependency = if ($GitRevision) { "https://github.com/blaze-tc/Human-Vision-SDK.git?path=/upm/com.blazetc.humanvision#$GitRevision" } else { "file:$package" }
@{dependencies=@{'com.blazetc.humanvision'=$dependency}} | ConvertTo-Json -Depth 4 | Set-Content "$testProject/Packages/manifest.json" -Encoding utf8
Copy-Item -LiteralPath "$PSScriptRoot/Upm040ImportCheck.cs" -Destination "$testProject/Assets/Editor/Upm040ImportCheck.cs" -Force
$arguments = "-batchmode -nographics -projectPath `"$testProject`" -executeMethod Upm040ImportCheck.Run -logFile `"$taskRoot/out/upm040-import.log`""
$startedAt = [DateTime]::UtcNow
$process = Start-Process -FilePath $Unity -ArgumentList $arguments -WindowStyle Hidden -PassThru
$process.WaitForExit()
if ($process.ExitCode -ne 0) { throw "UPM import failed ($($process.ExitCode)); see out/upm040-import.log" }
if (!(Test-Path "$testProject/upm-import-pass.txt")) { throw 'Missing UPM import success evidence' }
if ((Get-Item "$testProject/upm-import-pass.txt").LastWriteTimeUtc -lt $startedAt) { throw 'Stale UPM import evidence' }
if ($GitRevision) {
    $locked = (Get-Content "$testProject/Packages/packages-lock.json" -Raw | ConvertFrom-Json).dependencies.'com.blazetc.humanvision'
    if ($locked.source -ne 'git' -or $locked.hash -ne $GitRevision) { throw 'The tested package did not resolve to the requested Git commit' }
    Write-Output "Verified remote Git source: $($locked.hash)"
}
Get-Content "$testProject/upm-import-pass.txt"
