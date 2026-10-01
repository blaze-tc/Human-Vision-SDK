param(
    [string]$Unity = 'D:/Developer/2021.3.45f1/Editor/Unity.exe',
    [string]$EvidenceName = ('local-import-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$Package
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if ($EvidenceName -notmatch '^[a-zA-Z0-9_-]+$') { throw 'EvidenceName must be a single safe directory name.' }
$taskProject = Join-Path $taskRoot ('out/pc-demo/' + $EvidenceName)
if (Test-Path -LiteralPath $taskProject) { throw 'Use a new evidence directory; existing projects are preserved.' }
if (!$Package) { $Package = Join-Path $taskRoot 'upm/com.blazetc.humanvision.pc-demo' }
$Package = [IO.Path]::GetFullPath($Package).Replace('\','/')
New-Item -ItemType Directory -Path "$taskProject/Assets/Editor", "$taskProject/Packages", "$taskProject/ProjectSettings" -Force | Out-Null
@{dependencies=@{'com.blazetc.humanvision'=('file:'+$Package)}} | ConvertTo-Json -Depth 4 | Set-Content "$taskProject/Packages/manifest.json" -Encoding utf8
Set-Content "$taskProject/ProjectSettings/ProjectVersion.txt" 'm_EditorVersion: 2021.3.45f1' -Encoding utf8
Copy-Item -LiteralPath "$PSScriptRoot/PcDemoAcceptance.cs" -Destination "$taskProject/Assets/Editor/PcDemoAcceptance.cs"
Set-Content "$taskProject/.pc-demo-acceptance-project" 'isolated PC acceptance project' -Encoding utf8
$taskStart = [Diagnostics.ProcessStartInfo]::new()
$taskStart.FileName = $Unity
$taskStart.WorkingDirectory = $taskProject
$taskStart.UseShellExecute = $false
$taskStart.CreateNoWindow = $true
$taskStart.EnvironmentVariables['__COMPAT_LAYER'] = 'RunAsInvoker'
$taskStart.Arguments = '-batchmode -force-d3d11 -projectPath "'+$taskProject+'" -executeMethod PcDemoAcceptance.Run -logFile "'+$taskProject+'/unity.log"'
$taskProcess = [Diagnostics.Process]::Start($taskStart)
$taskRecord = @{pid=$taskProcess.Id;startedUtc=[DateTime]::UtcNow.ToString('o');project=$taskProject;package=$Package;log="$taskProject/unity.log"}
$taskRecord | ConvertTo-Json | Set-Content "$taskProject/launch.json" -Encoding utf8
$taskRecord | ConvertTo-Json
