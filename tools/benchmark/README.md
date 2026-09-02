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
