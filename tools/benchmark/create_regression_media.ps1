[CmdletBinding()]
param(
    [string]$FfmpegPath = "ffmpeg",
    [string]$FfprobePath = "ffprobe",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$testdata = Join-Path $projectRoot "tests\testdata"
$manifestPath = Join-Path $testdata "d0_3_video_manifest.json"
$ffmpegVersion = (& $FfmpegPath -version | Select-Object -First 1)
$records = @()
$inputs = @(
    @{
        Raw = Join-Path $testdata "d0_1_human_pose.bgr"
        Output = Join-Path $testdata "d0_3_one_person.mp4"
        Size = "218x346"
    },
    @{
        Raw = Join-Path $testdata "d0_2_two_people.bgr"
        Output = Join-Path $testdata "d0_3_two_people.mp4"
        Size = "436x346"
    }
)

foreach ($item in $inputs) {
    if (-not (Test-Path -LiteralPath $item.Raw -PathType Leaf)) {
        throw "Missing raw regression fixture: $($item.Raw)"
    }
    if ((Test-Path -LiteralPath $item.Output -PathType Leaf) -and -not $Force) {
        throw "Output already exists; pass -Force to regenerate: $($item.Output)"
    }
    & $FfmpegPath -y -v error -stream_loop 9 -f rawvideo -pixel_format bgr24 `
        -video_size $item.Size -framerate 5 -i $item.Raw -frames:v 10 -an `
        -c:v libx264 -preset veryslow -crf 18 -pix_fmt yuv420p -movflags +faststart `
        $item.Output
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed for $($item.Output)"
    }
    $frameCount = & $FfprobePath -v error -count_frames -select_streams v:0 `
        -show_entries stream=nb_read_frames -of default=nokey=1:noprint_wrappers=1 `
        $item.Output
    if ($LASTEXITCODE -ne 0 -or [int]$frameCount -ne 10) {
        throw "Expected 10 decoded frames in $($item.Output); received $frameCount"
    }
    $hash = (Get-FileHash -LiteralPath $item.Output -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceHash = (Get-FileHash -LiteralPath $item.Raw -Algorithm SHA256).Hash.ToLowerInvariant()
    $sizeParts = $item.Size.Split("x")
    $records += [ordered]@{
        filename = Split-Path -Leaf $item.Output
        sha256 = $hash
        source_raw = Split-Path -Leaf $item.Raw
        source_raw_sha256 = $sourceHash
        width = [int]$sizeParts[0]
        height = [int]$sizeParts[1]
        fps = 5
        frames = 10
        codec = "H.264/libx264 CRF 18 yuv420p"
    }
    Write-Host "$($item.Output) frames=10 sha256=$hash"
}

[ordered]@{
    schema_version = 1
    generator = "tools/benchmark/create_regression_media.ps1"
    ffmpeg_version = $ffmpegVersion
    official_reference_image_sha256 = "dd25fd8186e9ce27625520e24ac13ec6747316e51d20b124d3271c5764686d4e"
    clips = $records
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Host "Wrote $manifestPath"
