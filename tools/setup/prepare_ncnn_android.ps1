param(
    [ValidateSet('arm64-v8a')][string]$Abi = 'arm64-v8a',
    [ValidateSet(26)][int]$ApiLevel = 26,
    [string]$ArchivePath,
    [switch]$VerifyArchiveOnly,
    [string]$AndroidNdk = 'D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK',
    [string]$VisualStudio = 'D:/Microsoft Visual Studio',
    [ValidateRange(1,64)][int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = (Resolve-Path "$PSScriptRoot/../..").Path.Replace('\','/')
$provenancePath = "$root/third_party/ncnn/provenance.json"
$pin = Get-Content -LiteralPath $provenancePath -Raw | ConvertFrom-Json
$cache = "$root/out/ncnn-$($pin.version)"
New-Item -ItemType Directory -Force $cache | Out-Null
$downloadedTemporaryArchive = $null
if (-not $ArchivePath) {
    $ArchivePath = "$cache/$($pin.archive.name)"
    if (-not (Test-Path -LiteralPath $ArchivePath)) {
        $downloadedTemporaryArchive = "$ArchivePath.download"
        Invoke-WebRequest -Uri $pin.archive.url -OutFile $downloadedTemporaryArchive
        $ArchivePath = $downloadedTemporaryArchive
    }
}
$ArchivePath = (Resolve-Path -LiteralPath $ArchivePath).Path
if ((Get-Item -LiteralPath $ArchivePath).Length -ne $pin.archive.size) {
    throw "Archive size mismatch: expected $($pin.archive.size) bytes: $ArchivePath"
}
if ((Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $pin.archive.sha256) {
    throw "Archive SHA-256 mismatch: expected $($pin.archive.sha256): $ArchivePath"
}
if ($downloadedTemporaryArchive -and
    [IO.Path]::GetFullPath($ArchivePath) -eq [IO.Path]::GetFullPath($downloadedTemporaryArchive)) {
    $verifiedArchive = "$cache/$($pin.archive.name)"
    Move-Item -LiteralPath $ArchivePath -Destination $verifiedArchive -Force
    $ArchivePath = $verifiedArchive
}
Write-Output "Verified archive: $($pin.archive.size) bytes SHA256=$($pin.archive.sha256)"
if ($VerifyArchiveOnly) { return }

function Assert-Hash([string]$Path, [string]$Expected) {
    if (-not (Test-Path -LiteralPath $Path) -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Expected) {
        throw "Source/patch SHA-256 mismatch: $Path (expected $Expected)"
    }
}
Assert-Hash "$root/$($pin.license.path)" $pin.license.sha256
foreach ($license in $pin.bundled_licenses) { Assert-Hash "$root/$($license.path)" $license.sha256 }
foreach ($patch in $pin.patches) { Assert-Hash "$root/$($patch.path)" $patch.sha256 }
$ndkProperties = Get-Content -LiteralPath "$AndroidNdk/source.properties" -Raw
if ($ndkProperties -notmatch "Pkg.Revision\s*=\s*$([regex]::Escape($pin.ndk_version))(\s|$)") {
    throw "Pinned ncnn build requires Android NDK $($pin.ndk_version): $AndroidNdk"
}
$cmake = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
$ninja = "$VisualStudio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
foreach ($tool in @($cmake, $ninja, "$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe")) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "Missing build tool: $tool" }
}

$source = "$cache/source"
$build = "$cache/android-arm64-api26"
$install = "$build/install"
$receiptPath = "$build/build-receipt.json"
New-Item -ItemType Directory -Force $source | Out-Null
# Verify every cached source against the verified archive. Only audited patched
# files may differ; a partial previous extraction is repaired, never trusted.
$patchedFiles = @{}
foreach ($patch in $pin.patches) {
    foreach ($file in $patch.files) { $patchedFiles[$file.path] = $file }
}
$archive = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
$archiveFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
try {
    foreach ($entry in $archive.Entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $source $entry.FullName))
        if (-not $target.StartsWith([IO.Path]::GetFullPath($source) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe archive entry: $($entry.FullName)"
        }
        if ($entry.FullName.EndsWith('/')) { continue }
        [void]$archiveFiles.Add($target)
        $expected = $null
        if ($patchedFiles.ContainsKey($entry.FullName) -and (Test-Path -LiteralPath $target)) {
            $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
            $record = $patchedFiles[$entry.FullName]
            if ($actual -eq $record.after_sha256 -or $actual -eq $record.before_sha256) { continue }
        }
        if (Test-Path -LiteralPath $target) {
            $stream = $entry.Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $expected = [Convert]::ToHexString($hasher.ComputeHash($stream)).ToLowerInvariant() }
            finally { $hasher.Dispose(); $stream.Dispose() }
            Assert-Hash $target $expected
        } else {
            New-Item -ItemType Directory -Force ([IO.Path]::GetDirectoryName($target)) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $false)
        }
    }
} finally { $archive.Dispose() }
foreach ($cachedFile in Get-ChildItem -LiteralPath $source -File -Recurse -Force) {
    if (-not $archiveFiles.Contains($cachedFile.FullName)) {
        throw "Unexpected file in audited source tree: $($cachedFile.FullName)"
    }
}
foreach ($patch in $pin.patches) {
    $alreadyApplied = $true
    foreach ($file in $patch.files) {
        if ((Get-FileHash -LiteralPath "$source/$($file.path)").Hash.ToLowerInvariant() -ne $file.after_sha256) { $alreadyApplied = $false }
    }
    if (-not $alreadyApplied) {
        foreach ($file in $patch.files) { Assert-Hash "$source/$($file.path)" $file.before_sha256 }
        # git apply inside an ignored nested directory would skip files unless
        # --unsafe-paths is used; use --directory from the repository root instead.
        Push-Location $root
        try {
            & git apply --check --directory="out/ncnn-$($pin.version)/source" "$root/$($patch.path)"
            if ($LASTEXITCODE -ne 0) { throw "Audited patch does not apply: $($patch.path)" }
            & git apply --directory="out/ncnn-$($pin.version)/source" "$root/$($patch.path)"
            if ($LASTEXITCODE -ne 0) { throw "Audited patch application failed: $($patch.path)" }
        } finally { Pop-Location }
    }
    foreach ($file in $patch.files) { Assert-Hash "$source/$($file.path)" $file.after_sha256 }
}
Write-Output 'Verified cached upstream source and audited external-acquire patch.'
$configure = @('--fresh', '-S', $source, '-B', $build, '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_TOOLCHAIN_FILE=$AndroidNdk/build/cmake/android.toolchain.cmake",
    "-DANDROID_ABI=$Abi", "-DANDROID_PLATFORM=android-$ApiLevel", "-DCMAKE_INSTALL_PREFIX=$install")
foreach ($flag in $pin.build_flags.PSObject.Properties) { $configure += "-D$($flag.Name)=$($flag.Value)" }
& $cmake @configure > "$cache/configure.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "ncnn configure failed: $cache/configure.log" }
& $cmake --build $build --target install --parallel $Jobs > "$cache/build.log" 2>&1
if ($LASTEXITCODE -ne 0) { throw "ncnn build failed: $cache/build.log" }
$libraries = @(Get-ChildItem -LiteralPath "$install/lib" -Filter '*.a' | ForEach-Object {
    @{ path = "lib/$($_.Name)"; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
})
$receipt = [ordered]@{
    provenance_sha256 = (Get-FileHash -LiteralPath $provenancePath).Hash.ToLowerInvariant()
    source_commit = $pin.source_commit; archive_sha256 = $pin.archive.sha256
    patches = $pin.patches; android_abi = $Abi; android_api_level = $ApiLevel
    build_flags = $pin.build_flags; ndk_version = $pin.ndk_version
    cmake_version = (& $cmake --version | Select-Object -First 1)
    ninja_version = (& $ninja --version)
    clang_version = (& "$AndroidNdk/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe" --version | Select-Object -First 1)
    libraries = $libraries
}
$receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding utf8
Write-Output "ncnn Android ARM64 API 26 static Vulkan build ready: $install"
Write-Output "Build provenance and library hashes: $receiptPath"
