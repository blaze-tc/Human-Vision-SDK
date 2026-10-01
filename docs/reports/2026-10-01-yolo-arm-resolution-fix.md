# YOLO arm-resolution correction: explicit rectangle640x384

The user rejects the current seven-person arm following. The raw square320
measurements place raised wrists close to the torso. The overlay consumes raw
bodies; increasing smoothing or holding would not correct these measurements.

The existing immutable FP32 seven-640 eligibility archive is now supported by
the production decoder and GPU pipeline as an explicit 640x384 contract. This
local evaluation route accepts exact 16:9 landscape sources, resizes to640x360
and pads12 pixels above/below. Unsupported source aspect ratios fail clearly.
Square320/416 behavior remains available. The square640 route is not admitted.
Input/output dtype remains FP32 with explicit pack1 boundaries and5040 anchors.
AHB ownership retirement and cached GPU input remain unchanged. No fallback,
input CPU readback, confidence reduction or invented wrist measurement is used.

The semantic regression reads the real hash-bound video frame1500 outputs,
associates all seven measured boxes uniquely with annotated people, and requires
every anatomical left wrist above its corresponding left shoulder at the
existing0.2 joint-validity threshold. Square320 yields0/7; rectangle640x384
yields7/7. This checks a real raised-arm frame, not continuous motion accuracy.

Independent review found floating truncation could reject exact16:9 sources
624x351 and1072x603. RED reproduced the rejection. The decoder and preprocessing
now use exact360 resized height after the integer aspect-ratio proof, with
geometry and transform regressions for these and other dimensions.

Fresh verification commands:

```powershell
build/windows-test/bin/Release/humanvision_native_tests.exe --gtest_filter=Yolo*
py -3 -m unittest tests.reference.test_yolo_rectangle_stage tools.test.test_stage_android_yolo_eval
py -3 tools/test/verify_android_native.py --library build/android-yolo-m2/bin/Release/libhumanvision.so
```

Root reproduced13/13 YOLO tests after the rounding correction and6/6 staging tests,
and the final Android API26/ARM64/static-ncnn/1813-import audit. Implementer
reported120/120 broader native tests and17/17 Python tests. Independent scoped
spec compliance and code quality re-review passed, independently reproducing
13/13 YOLO and2/2 rectangle staging tests and verifying the final identities.

Final native SHA256:
`f759368c26cf9cc621a00113a50d9de439b259b74f2c045cecda91620ba479f0`.
Runtime source index SHA256:
`954b0fb5a152672b2242ccc14d49588be74e488000a110bbe7c91a022b143f00`.
Runtime artifacts: `out/android-yolo/runtime-rectangle640x384-arm-verified`.
These local weights are not redistributed. The native build also includes
preserved uncommitted R4 work; the SHA identifies the tested artifact, not a
claim that HEAD alone reproduces that artifact.

Continuous device following, jitter, independent person coverage and>=25 fresh
complete observation FPS remain pending. Existing25FPS source cannot certify30.
No merge to main, Release or completed physical acceptance is claimed.
