[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$version = '1.29.0'
$expectedSha256 = 'C9B4B7086B529AD814F428C1BAD028E20A25D7DC0699836775FAACE4AB5B78B2'
$expectedCommit = '2e2543fbe9fae542f921d47a72d21d5a4ef0b710'
$expectedRuntimeDllSha256 = '69D8E6D3879A3B4001CDC74C8ED9CCC7E7F799A5B847059738323404519EC471'
$url = "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$downloadDirectory = Join-Path $projectRoot 'build\downloads'
$archivePath = Join-Path $downloadDirectory "onnxruntime-win-x64-$version.zip"
$thirdPartyDirectory = Join-Path $projectRoot 'third_party'
$targetDirectory = Join-Path $thirdPartyDirectory 'onnxruntime'
$temporaryDirectory = Join-Path $projectRoot "build\onnxruntime-extract-$version"
$extractedDirectory = Join-Path $temporaryDirectory "onnxruntime-win-x64-$version"

if (Test-Path -LiteralPath $targetDirectory) {
    $commitFile = Join-Path $targetDirectory 'GIT_COMMIT_ID'
    $runtimeDll = Join-Path $targetDirectory 'lib\onnxruntime.dll'
    if ((Test-Path -LiteralPath $commitFile) -and (Test-Path -LiteralPath $runtimeDll)) {
        $actualCommit = (Get-Content -LiteralPath $commitFile -Raw).Trim()
        $actualRuntimeDllSha256 = (Get-FileHash -LiteralPath $runtimeDll -Algorithm SHA256).Hash
        if ($actualCommit -ne $expectedCommit -or
            $actualRuntimeDllSha256 -ne $expectedRuntimeDllSha256) {
            throw "Existing ONNX Runtime files do not match the locked 1.29.0 CPU x64 package: $targetDirectory"
        }
        Write-Output "ONNX Runtime already present: $targetDirectory"
        exit 0
    }
    throw "Existing ONNX Runtime directory is incomplete; inspect it before retrying: $targetDirectory"
}

New-Item -ItemType Directory -Force -Path $downloadDirectory, $thirdPartyDirectory | Out-Null
if (-not (Test-Path -LiteralPath $archivePath)) {
    Invoke-WebRequest -Uri $url -OutFile $archivePath
}

$actualSha256 = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "ONNX Runtime archive SHA-256 mismatch. Expected $expectedSha256, received $actualSha256"
}
if (Test-Path -LiteralPath $temporaryDirectory) {
    throw "Temporary extraction directory already exists; inspect it before retrying: $temporaryDirectory"
}

New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $temporaryDirectory
if (-not (Test-Path -LiteralPath $extractedDirectory)) {
    throw "Expected archive root is missing after extraction: $extractedDirectory"
}
Move-Item -LiteralPath $extractedDirectory -Destination $targetDirectory

Write-Output "Installed ONNX Runtime $version to $targetDirectory"
Write-Output "Archive SHA-256: $actualSha256"
