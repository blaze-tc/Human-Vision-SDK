# HumanVision test media

Place local regression media here. These files may be Git LFS/external artifacts depending on repository policy.

Minimum to start D0:

- `person_1.mp4` - one full-body participant
- `person_multi.mp4` - at least two participants

Recommended full matrix:

- `person_1.mp4`
- `person_2.mp4`
- `person_4.mp4`
- `far_person.mp4`
- `crossing_2.mp4`
- `occlusion.mp4`

For every test clip, record in a sidecar `.md` or test manifest:

- resolution/FPS
- number of people expected to be visible
- camera distance if known
- intended test purpose
- licensing/ownership status

Do not commit customer-sensitive or personally sensitive video unless repository access and consent are appropriate.

## D0.2 committed fixtures

- `d0_1_human_pose.bgr` is a raw BGR24 decode of the locked official MMDeploy `human-pose.jpg`; provenance, dimensions, and SHA-256 are recorded in `d0_2_fixtures.json`.
- `d0_2_two_people.bgr` is two horizontal copies of that same official image and verifies `MaxBodies` selection using two real model detections rather than fixed boxes.
- `add_one.onnx` is a 180-byte float32 `output = input + 1` backend fixture generated with ONNX opset 11. It tests generic ONNX Runtime load/run only and is never used as a detector result.
- `d0_2_fixture_contract.h` is generated from the official D0.1 PyTorch reference and feeds native preprocessing and detector golden tests without a runtime JSON dependency.

## D0.3 committed fixtures

- `d0_3_one_person.mp4` and `d0_3_two_people.mp4` are 10-frame H.264 clips encoded from the real raw fixtures for the isolated native video benchmark. They contain no fixed detector or pose results.
- `d0_3_fixture_contract.h` and `d0_3_fixtures.json` contain the pose affine, normalized input samples, and COCO-17 golden joints derived from the locked official D0.1 reference.
- `d0_3_video_manifest.json` records clip dimensions, FPS, frame count, source hashes, encoded hashes, codec settings, and FFmpeg version.

Regenerate the raw/model/golden fixtures with:

```powershell
.venv-reference\Scripts\python.exe -m tools.reference.create_native_fixtures
```

Regenerate the MP4 fixtures with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/benchmark/create_regression_media.ps1
```
