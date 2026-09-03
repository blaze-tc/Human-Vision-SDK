param(
    [Parameter(Mandatory = $true)]
    [string]$DumpbinPath,

    [Parameter(Mandatory = $true)]
    [string]$HumanVisionDll,

    [Parameter(Mandatory = $true)]
    [string]$PrivateRuntimeDll
)

$ErrorActionPreference = 'Stop'

foreach ($requiredPath in @($DumpbinPath, $HumanVisionDll)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required file is missing: $requiredPath"
    }
}

$dependencyOutput = & $DumpbinPath /nologo /dependents $HumanVisionDll 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed for '$HumanVisionDll':`n$($dependencyOutput -join [Environment]::NewLine)"
}

$dependencyText = $dependencyOutput -join [Environment]::NewLine
if ($dependencyText -notmatch '(?im)^\s+humanvision_onnxruntime\.dll\s*$') {
    throw "humanvision.dll does not depend on the private humanvision_onnxruntime.dll."
}

if ($dependencyText -match '(?im)^\s+onnxruntime\.dll\s*$') {
    throw "humanvision.dll still depends on the shared onnxruntime.dll name."
}

if (-not (Test-Path -LiteralPath $PrivateRuntimeDll -PathType Leaf)) {
    throw "Private ONNX Runtime DLL is missing beside humanvision.dll: $PrivateRuntimeDll"
}

Write-Host "HumanVision ONNX Runtime dependency is isolated: $PrivateRuntimeDll"
