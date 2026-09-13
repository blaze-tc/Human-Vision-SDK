# Native video benchmark

`hv_video_benchmark` uses the public asynchronous C ABI. FFmpeg is invoked only
as an isolated regression-media reader; the HumanVisionCore model pipeline has
no FFmpeg dependency.

Generate the committed one-person and two-person MP4 fixtures from the locked
official OpenMMLab reference image:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/benchmark/create_regression_media.ps1
```

Example Release run:

```powershell
build/windows-release/bin/Release/hv_video_benchmark.exe `
  --input tests/testdata/d0_3_two_people.mp4 --width 436 --height 346 `
  --fps 5 --frames 10 --max-bodies 2 `
  --detector-model models/detector/rtmdet_tiny_640.onnx `
  --pose-model models/pose/rtmpose_s_256x192.onnx `
  --output-prefix out/benchmark/d0_3_two_people_max2
```

The tool writes per-frame CSV timing and a JSON summary containing body-count,
track-ID, joint-validity, stage timing, and first-frame real body/joint samples.

`check_android_model_ep.py` runs ONNX Runtime's official mobile usability checker
for the four shipped Android body, detector, and hand models and writes focused
NNAPI coverage reports. Use a Python environment containing the pinned checker:

```powershell
$env:PYTHONPATH = (Resolve-Path out/ort-checker-1.23.0).Path
& .venv-reference/Scripts/python.exe tools/benchmark/check_android_model_ep.py `
  --python .venv-reference/Scripts/python.exe
```

The reports record static partitions, node coverage, dynamic-shape caveats and
the checker recommendation. They do not establish Android device throughput.
