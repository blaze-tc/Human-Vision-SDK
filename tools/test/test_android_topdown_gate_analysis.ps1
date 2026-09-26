$ErrorActionPreference = 'Stop'
$python = Join-Path (Resolve-Path "$PSScriptRoot/../..").Path '.venv-reference/Scripts/python.exe'
if (!(Test-Path -LiteralPath $python)) { $python = 'python' }
& $python -m tools.test.test_android_topdown_gate_analysis -v
exit $LASTEXITCODE
