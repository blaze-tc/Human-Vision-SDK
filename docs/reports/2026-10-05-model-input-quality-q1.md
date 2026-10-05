# Fixed 960x576 model-input admission: Q1

Q1 implements the high input geometry requested for the upcoming shared
High/Medium/Low selector. This commit does not add the selector or change the
default640x384 profile. Target choices are960x576/640x384/512x288 respectively.

## Implemented contract

The existing YOLO pipeline admits exact960x576 with11340 stride8/16/32 rows.
16:9 input resizes without stretch to960x540, then pads18px top/bottom with114.
Nearby dimensions,960square and non16:9 sources reject before backend creation.
out0/out1 remain exact FP32 pack1 tensors2948400/2313360 bytes. Model graph,
weights, backend options, production GPU/AHB input and public/plugin ABI remain
unchanged. Decoder buffers are reused after warm-up for capacities1..8.

The offline runner is separate from the historical runner. A keyword-only
internal recipe binding preserves all older default evidence checks; historical
runner source/CMake hashes remain unchanged. No CLI recipe override was added.

## Verification

Executed from the active isolated worktree on2026-10-05:

```powershell
cmd /c .superpowers\sdd\2026-10-05-model-input-quality\task-q1\build.cmd
build/windows-test/bin/Release/humanvision_native_tests.exe --gtest_filter=Yolo*
ctest --test-dir build/windows-test -C Release --output-on-failure
py -3.13 -m unittest discover -s tests/reference -p 'test_yolo*.py' -v
cmake --build build/android-yolo-rectangle576
py -3.13 tools/test/verify_android_native.py --library build/android-yolo-rectangle576/bin/Release/libhumanvision.so
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
```

Native behavioral RED:2/2 fail before admission; GREEN after implementation.
Final affected host build passed; YOLO32/32 and complete Release CTest368/368.
Canonical Python3.13.13/numpy2.4.4/OpenCV4.13.0 tests127/127 passed.
Independent spec and code-quality reviewer reran YOLO32/32, Python127/127,
architecture/public-surface/catalog guards, frozen-artifact validation and
whitespace checks: Spec PASS / Quality PASS. Reviewer did not repeat the build
or device executions. Initial compiler mismatch and Python3.10 environment
failures are retained in the ignored task evidence; no gate was weakened.

The first exact staged review rejected automatic Git LF normalization of the
three new byte-pinned artifacts: evidence index, runner source and CMake recipe.
Narrow `-text` attributes preserve their actual captured CRLF bytes. An isolated
Git index regression first reproduced normalized hash mismatches (RED),
then verified stored and checked-out byte identities (GREEN), without changing
historical runners or rewriting capture records. Shipping-source archives must
therefore retain the reviewed frozen identities as well as semantic code.

Fresh Snapdragon888/Adreno660 offline CPU/Vulkan captures passed3/3: original
seven-person frame1500 detects7 and covers7 annotations/raised left arms in
both modes; one-person control detects1; empty detects0. The one-person control
is explicitly a portrait-derived centered16:9 pad114 canvas with original pixels
unchanged, not camera-stream evidence. Worst raw max error0.000217437744140625,
mean1.7784163471843121e-6; historical limits remain unchanged.

Frozen960 evidence index:
`b1d634d79c6793bb0970e49b7b3975e901b9560a3f38f9f581e57e554de82683`.
Runner binary:
`7177b482665a1a3607de8eb5b086027572e851e2d010236f312d5558598ea863`.
Original runner source/CMake identities:
`5d9a2b3ae19239d22f66d77fe6bba52206c700c522fed93810ede4a839d37814` /
`531f7fa450bbc2edc4108b9e92a61e49d58a49a8ced03465c554fd19afae0a46`.
Pinned revision: `f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca`.
Model param/weights identities are retained in the frozen index; same as640.

## Limits and next work

The affected Android library built and passed ELF64/AArch64/API26/import audit,
but its build includes unrelated dirty sources. SHA256
`50872a60ed55790ea3cd1d04ae7069a0e7916ddc7cf93f59b58080f4053f4fb5`
is compilation evidence only, not a shipping binary. No new APK was built or
installed. No production GPU preprocessing parity, sustained FPS, Region motion,
thermal, temporal stability or general accuracy improvement is established.

Q2 adds actual catalog/profiles/staging and same-backend build admission; Q3 adds
saved UGUI selection and explicit Apply; Q4 qualifies clean source/native/package
closure and real Unity/Android behavior. Task11 and30 fresh complete observation
frames/s remain open. Existing PC/ORT profiles remain fixed and cannot advertise
unsupported quality shapes. No main merge, release or redistribution approval.
Detailed local records: `.superpowers/sdd/2026-10-05-model-input-quality/`.
