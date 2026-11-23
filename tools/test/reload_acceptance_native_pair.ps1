param(
    [Parameter(Mandatory=$true)][string]$RequestPath,
    [int]$EditorPid = 0,
    [string]$UnityExe = '',
    [string]$Token = '',
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
$expectedRoot = [IO.Path]::GetFullPath('E:/UnityProject/Human-Vision-SDK-Test').TrimEnd('\','/')
function Absolute([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path) -or -not [IO.Path]::IsPathRooted($path)) { throw 'Every request path must be absolute.' }
    [IO.Path]::GetFullPath($path).TrimEnd('\','/')
}
function Same([string]$left,[string]$right) { [string]::Equals((Absolute $left),(Absolute $right),[StringComparison]::OrdinalIgnoreCase) }
function UnderBuilds([string]$path) {
    $value = Absolute $path
    if (-not $value.StartsWith($script:builds + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Status, logs and backups must stay under this project Builds folder.' }
    $value
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function WriteStatus([string]$phase,[string]$message) {
    $data = @{version=1;phase=$phase;token=$Token;editorPid=$EditorPid;message=$message;utc=[DateTime]::UtcNow.ToString('o')}
    $temporary = $script:status + '.tmp.' + $Token
    [IO.File]::WriteAllText($temporary,($data | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temporary,$script:status,$true)
}
function AssertNotCancelled {
    if (Test-Path -LiteralPath ($script:status + '.cancel.' + $Token)) { throw 'The invoking Editor cancelled this reload; no delayed reload is authorized.' }
}
function RequireExitCommit {
    AssertNotCancelled
    if (-not (Test-Path -LiteralPath ($script:status + '.commit.' + $Token) -PathType Leaf)) { throw 'The original Editor exited without committing this reload.' }
}
$status = $null
$replaced = $false
$launched = $false
$pairs = @()
try {
    $request = Get-Content -LiteralPath (Absolute $RequestPath) -Raw | ConvertFrom-Json
    if ($request.version -ne 1 -or -not (Same $request.projectRoot $expectedRoot)) { throw 'Only the version-1 Human-Vision-SDK-Test reload request is supported.' }
    if (-not (Same $request.helperScript $PSCommandPath)) { throw 'The request helper does not match this reviewed script.' }
    $pwsh = Absolute $request.powerShellExe
    if (-not (Test-Path -LiteralPath $pwsh -PathType Leaf) -or [IO.Path]::GetFileName($pwsh) -ine 'pwsh.exe') { throw 'A real absolute pwsh.exe path is required.' }
    $builds = Join-Path $expectedRoot 'Builds'
    $status = UnderBuilds $request.statusPath
    $backup = UnderBuilds $request.backupRoot
    $unityLog = UnderBuilds $request.unityLogPath
    if ((Same $status $unityLog) -or (Same $status $RequestPath) -or (Same $unityLog $RequestPath)) { throw 'Request, status and Editor log paths must be distinct.' }
    $sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../out/original-resolution-20261124'))
    foreach ($kind in @('sdk','input')) {
        $name = if ($kind -eq 'sdk') { 'humanvision.dll' } else { 'humanvision_input.dll' }
        $sourceName = if ($kind -eq 'sdk') { 'sdk-windows.dll' } else { 'input-windows.dll' }
        $source = Absolute $request.($kind + 'Source')
        $target = Absolute $request.($kind + 'Target')
        $sha = [string]$request.($kind + 'Sha256')
        if (-not (Same $source (Join-Path $sourceRoot $sourceName)) -or
            -not (Same $target (Join-Path $expectedRoot ('Assets/Plugins/x86_64/' + $name)))) { throw 'The DLL pair must use the qualified artifact sources and exact acceptance plugin targets.' }
        if ($sha -notmatch '^[a-fA-F0-9]{64}$' -or (Hash $source) -ine $sha) { throw "Qualified $kind source hash mismatch." }
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "The existing $kind plugin is missing; refusing an unbacked replacement." }
        $pairs += @{source=$source;target=$target;sha=$sha.ToLowerInvariant();backup=(Join-Path $backup $name);previous=(Hash $target)}
    }
    if ($ValidateOnly) {
        @{valid=$true;projectRoot=$expectedRoot;files=$pairs;scope='Validation only; no files written and no process started or stopped.'} | ConvertTo-Json -Depth 4
        return
    }
    if ($EditorPid -le 0 -or $Token -notmatch '^[a-f0-9]{32}$') { throw 'A live Editor PID and fresh handshake token are required.' }
    $unity = Absolute $UnityExe
    if (-not (Test-Path -LiteralPath $unity -PathType Leaf) -or [IO.Path]::GetFileName($unity) -ine 'Unity.exe') { throw 'Use EditorApplication.applicationPath for Unity.exe.' }
    $editor = Get-Process -Id $EditorPid
    if (-not (Same $editor.Path $unity)) { throw 'The waiting PID is not the invoking Unity executable.' }
    New-Item -ItemType Directory -Force -Path $backup,(Split-Path $status),(Split-Path $unityLog) | Out-Null
    foreach ($pair in $pairs) {
        if (Test-Path -LiteralPath $pair.backup) {
            if ((Hash $pair.backup) -ine $pair.previous) { throw 'An immutable backup already exists for a different plugin pair.' }
        } else { Copy-Item -LiteralPath $pair.target -Destination $pair.backup }
        if ((Hash $pair.backup) -ine $pair.previous) { throw 'Backup hash verification failed.' }
    }
    WriteStatus 'ready' 'Both source hashes and immutable backups verified; waiting for the original Editor to exit naturally.'
    $exitDeadline = [DateTime]::UtcNow.AddSeconds(180)
    while (-not $editor.WaitForExit(250)) {
        AssertNotCancelled
        if ([DateTime]::UtcNow -gt $exitDeadline) { throw 'Timed out waiting for the original Editor; no plugin was replaced.' }
    }
    if (-not $editor.HasExited) { throw 'The original Editor has not exited; no plugin was replaced.' }
    RequireExitCommit
    foreach ($pair in $pairs) {
        AssertNotCancelled
        if ((Hash $pair.source) -ine $pair.sha) { throw 'A qualified artifact changed after the ready handshake.' }
    }
    $replaced = $true
    foreach ($pair in $pairs) { AssertNotCancelled; Copy-Item -LiteralPath $pair.source -Destination $pair.target -Force }
    foreach ($pair in $pairs) { if ((Hash $pair.target) -ine $pair.sha) { throw 'Installed DLL pair hash verification failed.' } }
    WriteStatus 'installed' 'Both DLL hashes verified; starting the visible acceptance Editor.'
    RequireExitCommit
    $arguments = @('-projectPath',('"' + $expectedRoot + '"'),'-logFile',('"' + $unityLog + '"'))
    $newEditor = Start-Process -FilePath $unity -ArgumentList $arguments -WorkingDirectory $expectedRoot -WindowStyle Normal -PassThru
    $launched = $true
    WriteStatus 'launched' ('Verified DLL pair installed; visible Unity PID=' + $newEditor.Id)
} catch {
    $message = $_.Exception.Message
    if ($replaced -and -not $launched) {
        try {
            foreach ($pair in $pairs) { Copy-Item -LiteralPath $pair.backup -Destination $pair.target -Force }
            foreach ($pair in $pairs) { if ((Hash $pair.target) -ine $pair.previous) { throw 'Rollback hash mismatch.' } }
            $message += ' Previous DLL pair restored; the Editor was not automatically restarted.'
        } catch { $message += ' Rollback needs manual recovery from the verified backup: ' + $_.Exception.Message }
    }
    if (-not $ValidateOnly -and $status -and (Test-Path -LiteralPath (Split-Path $status))) {
        try { WriteStatus 'failed' $message } catch { }
    }
    Write-Error $message
    exit 1
}
