# Explicit rectangle512x288 SDK integration

The local FP32 ModelPack can now select512x288 as an explicitly validated16:9
route. It uses3,024 anchors, exact512x288 GPU resize, no padding, and the original
five FP32 backend options. Existing320/416,640x384 and explicit640 SGEMM routes
retain their contracts. Portrait sources and square512 are rejected; SGEMM512
remains unqualified. No public API, decoder thresholds, Tracker, Region,
renderer or GPU synchronization behavior changes.

The builder validates the frozen two-device-archive index, recalculates numerical
and seven raised-arm results, and verifies source, model, runner, output and
profile bindings before creating a fresh runtime root. Unity staging checks the
exact native binary, four selected files and runner provenance; both project
copy checks validate the explicit input contract.

Fresh verification:

```powershell
py -3 -m unittest discover -s tests/reference -p 'test_yolo_*.py' -q
ctest --test-dir build/windows-test -C Release --output-on-failure -R 'Ncnn|Yolo|GpuTrack|TopDown|UnityVulkanBridge|AhbSlotRing|GpuAbi'
py -3 tools/test/verify_android_native.py --library build/android-yolo-rectangle512/bin/Release/libhumanvision.so
py -3 tools/test/stage_android_yolo_eval.py --runtime out/android-yolo/runtime-rectangle512x288-arm-verified --size 512 --verify-only
```

Results:56 Python tests and156 native tests pass. ARM64/API26 audit resolves1,813
strong imports. The separate normal build disables trace, parity and GPU gate
instrumentation. Geometry and transform tests cover exact approved512 behavior,
unapproved source/shape rejection, bounded outputs and640 rounding regressions.
Builder tests reject tampered archive, source, limits and execution data; legacy
runtime file identities remain unchanged.

Native SHA256:
`f8a34538c8892b7bc626a2fa11ef5b0598e3b8c774b6e3cbd57f1f9377a50f4d`.
Runtime index SHA256:
`d37a27c67b74ebbf29866a6cce252af8dabc1ae8bcedddd0c2c5fbada5efc317`.

Actual Unity GPU-AHB continuous-video FPS and alignment are pending. Offline
single-frame eligibility does not establish other gestures, production GPU input
accuracy, or physical acceptance. The25FPS/30FPS performance goals remain unmet.
No main merge or Release is authorized. See the [candidate evidence](2026-10-01-yolo-rectangle512-eligibility.md).
