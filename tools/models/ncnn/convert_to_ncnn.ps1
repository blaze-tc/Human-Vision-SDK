param(
    [Parameter(Mandatory = $true)][ValidateSet('detector', 'body')][string]$Role,
    [Parameter(Mandatory = $true)][string]$Checkpoint,
    [Parameter(Mandatory = $true)][string]$Onnx,
    [Parameter(Mandatory = $true)][string]$ExportManifest,
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$FixtureSha256,
    [Parameter(Mandatory = $true)][string]$Onnx2Ncnn,
    [Parameter(Mandatory = $true)][string]$Onnx2NcnnSha256,
    [Parameter(Mandatory = $true)][string]$NcnnOptimize,
    [Parameter(Mandatory = $true)][string]$NcnnOptimizeSha256,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$NcnnToolVersion,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$Python = '.venv-reference/Scripts/python.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Hash([string]$Path, [string]$Expected, [string]$Label) {
    if ($Expected -cnotmatch '^[a-f0-9]{64}$') { throw "$Label SHA-256 is missing or malformed" }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -cne $Expected) { throw "$Label SHA-256 mismatch: $actual != $Expected" }
}

$manifest = Get-Content -LiteralPath $ExportManifest -Raw | ConvertFrom-Json
if ($manifest.role -cne $Role) { throw 'Export manifest role mismatch' }
Assert-Hash $Checkpoint $manifest.checkpoint_sha256 'checkpoint'
Assert-Hash $Onnx $manifest.onnx_sha256 'ONNX'
Assert-Hash $Fixture $FixtureSha256 'fixture'
Assert-Hash $Onnx2Ncnn $Onnx2NcnnSha256 'onnx2ncnn tool'
Assert-Hash $NcnnOptimize $NcnnOptimizeSha256 'ncnnoptimize tool'
& $Python -m tools.models.ncnn.model_contract validate-export --manifest $ExportManifest --role $Role --checkpoint $Checkpoint --onnx $Onnx
if ($LASTEXITCODE -ne 0) { throw 'Pinned export manifest preflight failed before conversion tools ran' }

$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $output | Out-Null
$rawParam = Join-Path $output 'raw.param'
$rawBin = Join-Path $output 'raw.bin'
$paramPath = Join-Path $output 'model.param'
$binPath = Join-Path $output 'model.bin'

& $Onnx2Ncnn $Onnx $rawParam $rawBin
if ($LASTEXITCODE -ne 0) { throw "onnx2ncnn exited $LASTEXITCODE" }
# 65536 selects fp16 weight storage in official ncnnoptimize. Runtime packing and
# VkMat FP16 conversion remain explicit responsibilities of the native backend.
& $NcnnOptimize $rawParam $rawBin $paramPath $binPath 65536
if ($LASTEXITCODE -ne 0) { throw "ncnnoptimize exited $LASTEXITCODE" }

$draft = [ordered]@{
    schema_version = 1
    role = $Role
    source_revisions = $manifest.source_revisions
    source_url = $manifest.source_url
    license = $manifest.license
    artifacts = [ordered]@{
        checkpoint_sha256 = $manifest.checkpoint_sha256
        onnx_sha256 = $manifest.onnx_sha256
        fixture_sha256 = $FixtureSha256
        param_sha256 = (Get-FileHash -LiteralPath $paramPath -Algorithm SHA256).Hash.ToLowerInvariant()
        bin_sha256 = (Get-FileHash -LiteralPath $binPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    ncnn_tool_version = $NcnnToolVersion
    tool_version = $NcnnToolVersion
    onnx2ncnn_sha256 = $Onnx2NcnnSha256
    ncnnoptimize_sha256 = $NcnnOptimizeSha256
    opset = $manifest.opset
    input_contract = $manifest.input_contract
    output_blobs = $manifest.output_blobs
    output_contract = $manifest.output_contract
    export_command = $manifest.export_command
    conversion_command = 'onnx2ncnn <pinned-onnx> raw.param raw.bin; ncnnoptimize raw.param raw.bin model.param model.bin 65536'
}
$draftPath = Join-Path $output 'model-conversion.json'
$draft | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $draftPath -Encoding utf8
& $Python -m tools.models.ncnn.audit_ncnn_graph --manifest $draftPath --onnx $Onnx --param $paramPath --checkpoint $Checkpoint --bin $binPath --fixture $Fixture --onnx2ncnn $Onnx2Ncnn --ncnnoptimize $NcnnOptimize
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $draftPath
    throw 'Graph audit failed; conversion manifest was not accepted'
}
Write-Output $draftPath
