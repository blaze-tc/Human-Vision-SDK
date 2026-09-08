param([string]$ToolRoot = 'D:/Microsoft Visual Studio/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot/../.."
$directory = Join-Path $root 'out/live-deps/ffmpeg-windows'
foreach ($component in @('avformat','avcodec','avutil','swscale')) {
    $dll = Get-ChildItem -LiteralPath $directory -Filter "$component-*.dll" | Select-Object -First 1
    $lines = & "$ToolRoot/dumpbin.exe" /nologo /exports $dll.FullName
    $symbols = @($lines | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\w+)') { $Matches[1] } })
    if ($symbols.Count -eq 0) { throw "No exports in $($dll.Name)" }
    $definition = Join-Path $directory "$component.def"
    @("LIBRARY $($dll.Name)", 'EXPORTS') + $symbols | Set-Content -LiteralPath $definition -Encoding ascii
    & "$ToolRoot/lib.exe" /nologo "/def:$definition" "/out:$directory/$component.lib" /machine:x64
    if ($LASTEXITCODE -ne 0) { throw 'Import-library creation failed' }
}
