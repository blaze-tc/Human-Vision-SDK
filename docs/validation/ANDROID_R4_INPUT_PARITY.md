# Revision 4 Task 1 — exact offline input fixtures (2026-09-26)

This report pins **offline** decoded source bytes and detector input tensors for
R4.1. It does not establish Unity VideoPlayer byte identity, Android GPU parity,
person detection on device, or physical acceptance. The Revision 3 integrated
gate remains FAIL. Task 2 must upload these goldens through the production GPU
route; Task 3 must separately latch and compare the actual VideoPlayer texture.

## Sources and human annotations

I inspected exact decoded frame 150 from both supplied videos at source resolution.
The `video-1.mp4` frame (1080×1884 portrait) visibly contains five complete
people. The `video-2.mp4` frame (1920×1080 landscape) contains six complete
children and one foreground adult whose legs leave the bottom edge; one rear
child overlaps a front child. The manifest stores manually estimated source-pixel
bounding boxes and visibility notes for **all** visible people. These boxes are
human annotations, not detector output or pose ground truth. The frame called
“full-person” contains multiple people; it is not a single-person fixture.

| Source | Video SHA-256 | Frame | Decoded RGBA SHA-256 | FP32 tensor SHA-256 | FP16 RTZ tensor SHA-256 | People |
| --- | --- | ---: | --- | --- | --- | ---: |
| `video-1.mp4` | `55cd66ae01696939a9c78ab55039e4011c2be4b8449e9e1c893959bc0d1975e0` | 150 | `6b1a0f7d5f07ef160ecfa36d7d748231239a00bae9aa38a305ce0fba9bc36b40` | `571c836eddc943db8269e5d46deff131e9ea46c2b428b044d43362c77d3ebc71` | `33d48d56b6decffd19c8fa2b142bc701b684a4454022f27ca39e6f7601a07228` | 5 |
| `video-2.mp4` | `6abd4a523e9e0dc9961a3f037e0c33600271ff3a53d170e5f1dbd8c562480f53` | 150 | `9291077b50f995044116342737c1e6b3a6b60f274b5f66f4afb5222b9569319e` | `9c5ed7d87ec15e69ef10abe3d4a51a30896e70ae8247da71a8b383e8865b102d` | `8e0b067d4aa9c8893aef9a3a65b5709f5eb807024da839e7daeb83148d52507d` | 7 |

The generated manifests and raw files are ignored under
`out/android-r4/video-1-frame-150/` and `out/android-r4/video-2-frame-150/`.
Manifest SHA-256 values are `cb0d101de39d7a2c5aa98a1c0831828178828c528f7352a661264813206c31ca`
and `05143fb8291beb190d79e5e7434c1ed63962daee53c6ddde7e130aa622e8a392`,
respectively. No video frames or tensors are committed.

## Analytic upload fixtures

`tools/test/r4_fixture_manifest.py --analytic` writes an asymmetric 5×3 RGB
corner/color pattern and a non-square 7×4 unique-color grid. Each source is
rotated clockwise by 0/90/180/270 degrees, then either left unmirrored or
mirrored horizontally, for 16 ignored cases. Each case contains exact RGBA8,
detector FP32 and FP16 RTZ files, source/derived shapes and strides, hashes,
and five named landmark mappings (source coordinate, transformed coordinate,
RGBA value). The manifest at `out/android-r4/analytic/manifest.json` has SHA-256
`11f772fe4d375dfafdb6a91ed22e2dc5984e373b1bf91a580027659700629635`.
This supplies uploadable known colors/locations for Task 2; it does not imply
any GPU path passed. Tests independently pin hand-derived landmark coordinates
for all eight transform choices. A separate saved-output pixel oracle reads the
RGBA files and checks literal source colors at hand-derived expected coordinates
for all five landmarks in all 16 cases. Deliberately generating mirrored cases
without mirroring made that oracle fail 40 comparisons; the generator's existing
self-validation would otherwise share the same faulty transform helper.

## Decode and tensor contract

`tools/test/r4_fixture_manifest.py` sequentially decodes from frame zero
through index 150 using OpenCV 4.10.0 with FFmpeg backend (`avcodec 58.134.100`,
`avformat 58.76.100`, `avutil 56.70.100`). It writes top-left-row RGBA8,
full-range RGB, opaque alpha, packed row stride `width×4`. OpenCV's decoded RGB
values are labelled `decoded_srgb_assumed`; the MP4's color metadata and
VideoPlayer conversion are not asserted identical by this label.

The independently computed detector input uses the pre-existing pinned ONNX
reference: bilinear letterbox to 320×320, RGB 114 padding, RGB order, means
`[123.675,116.28,103.53]`, scales
`[0.017124753831663668,0.01750700280112045,0.017429193899782137]`,
NCHW planar FP32, then observed Snapdragon FP32→FP16 round-toward-zero
packing (`elempack=1`). The portrait content is 183×320 with 68-pixel left pad;
landscape content is 320×180 with 70-pixel top pad. The checked detector
input contract identifies model pack `precision-t-26-ncnn-fp16`, detector param
SHA-256 `9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3`,
bin SHA-256 `4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da`,
and the checked profile SHA-256 is
`20e2714759f03fee79a94d0dcf7ccc80700785a2cb191a4a01b403e0b975d0b2`.
The local model
files were hash checked against the contract when generated.

The manifest declares clockwise rotations 0/90/180/270 followed by optional
horizontal preview mirror. The analytic 5×3 asymmetric grid tests corner and
interior point positions across all eight combinations. It is a transform
contract for subsequent GPU tests, not evidence that the current GPU path
already handles each combination.

## Verification

- Focused RED: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_r4_fixture_manifest.py -v` failed because the manifest module was absent; log `out/android-r4/task-1/red.log`.
- Focused GREEN: the same command passed 11/11. Tests reject altered bytes, changed frame index, missing/wrong decoder, shape/stride errors, nonfinite tensors even with a renewed hash, and changed fixture identity. Separate oracles check padding, RGB channels, sampled FP16 RTZ bits and saved RGBA colors at all eight geometry mappings. Controlled wrong-mirror RED and normal GREEN logs are `out/android-r4/task-1/review-oracle-red.log` and `review-oracle-green.log`.
- Analytic generation: `.venv-reference/Scripts/python.exe tools/test/r4_fixture_manifest.py --analytic --output-dir out/android-r4/analytic` wrote 16 cases with the manifest hash above; a saved-manifest validation pass checked every artifact byte.
- Existing video reference: `.venv-reference/Scripts/python.exe tools/test/topdown_video_reference.py --model out/c2-local-detector/rebuild/rtmdet-nano.onnx --video 'E:\Project\Human Vision SDK\video-1.mp4' --video 'E:\Project\Human Vision SDK\video-2.mp4' --seconds 5` passed; its frame-150 FP32 hashes equal the manifest hashes. The pinned ONNX scores are 0.646 and 0.744 respectively, without any Android GPU claim.
- Existing prepared detector golden: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_prepared_gate_golden.py -v` passed 2/2.
- Existing ncnn model contract: `.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -p test_ncnn_model_contract.py -v` passed 30/30. `.venv-reference/Scripts/python.exe tools/maintenance/check_architecture_boundaries.py` passed.

The exact commands and final check results are also in `docs/DEVELOPMENT_STATUS.md`.
