# Region candidate admission correction

The selected four Regions can lose rear-row occupants because full-frame YOLO
decoding truncates candidates to the public people limit before Region assignment.
After confidence NMS, candidates are ordered by source bounding-box area; larger
foreground detections can consume that limit. This is a reproduced source-level
mechanism, not proof that it explains every switch in the installed Android app.

## Bounded change

The composition root now supplies `pipeline.yolo.pose` with a candidate budget
equal to `min(HV_MAX_PEOPLE, ModelPack.max_people, plugin.max_people)`. It builds
this configuration at Start, before the later SetRegions call. Public capacity,
Profile capacity, BodyServices capacity and Region count limits remain unchanged.
TopDown keeps the requested public capacity because that also bounds pose ROI
work. The GPU Host output guard and decoder still enforce the configured budget.

With no Regions, the original area-ordered first-four public subset is preserved.
With four Regions, all admitted candidates participate in the existing assignment,
and at most four Bodies are published. No tracker, renderer, model, preprocessing,
public ABI, fallback policy or CPU frame-readback changes are included.

`detail::BuildGpuPipelineConfig` is an internal configuration boundary used by
the actual RuntimeSession Start method and by the focused regression. It does
not introduce a public API or per-frame configuration allocation.

## Verification

Checkout: `E:/Project/Human Vision SDK/.worktrees/android-ncnn-vulkan`.
Base HEAD: `8a850d4aeb87f79c6cfb5cb563695033885603fb`.
Retained ignored implementation and independent review logs are under
`.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp/`, in
`region-candidate-fix/` and `region-candidate-review/` respectively.

Regression uses real Profile/ModelPack/plugin resolution at public capacity4,
the production config builder, actual GPU Host and YOLO Create/Process/decoder,
then Region assignment and BodyServices. Hardware inference and frame delivery
use immutable saved genuine seven-person tensor views and a labelled test source.
Fixture hashes, original frame1500 geometry, seven independent annotations and
literal exclusive Regions are checked; boxes and joints are not fabricated.

Behavior-preserving builder extraction preceded RED. Four of six tests then
failed as expected: AllRear and MixedRows produced one occupied Region instead
of four; no-Region and smaller-metadata candidate admission remained limited to
four. TopDown and actual oversize-output rejection controls passed.

After the repair, focused tests pass6/6 and the full configured Release CTest
passes365/365 in25.55s. Independent spec and quality review both PASS, including
fresh focused6/6 replay, architecture/public-surface guard and whitespace check.
The independent reviewer inspected the full-suite log rather than rerunning it.

```powershell
cmd /c '"D:\Microsoft Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -vcvars_ver=14.44 >nul && "D:\Microsoft Visual Studio\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build out/input-sdk-host --config Release'
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' --test-dir out/input-sdk-host -C Release -R 'YoloRegionCandidate' --output-on-failure
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' --test-dir out/input-sdk-host -C Release --output-on-failure
py -3 tools/maintenance/check_architecture_boundaries.py
git diff --check -- runtime/composition/session.cpp runtime/composition/session.h tests/native/CMakeLists.txt
```

Reduced pack/plugin budgets4 and6 are honored. The unchanged Host rejects a
nonconforming plugin producing seven real candidates against budget4. A direct
config4 diagnostic remains a starvation control; the tests do not bypass the
production configuration by simply hard-coding8 everywhere.

## Acceptance limits

The host build includes existing unrelated dirty sources and is not a qualified
Android shipping source closure. This regression is not full RuntimeSession
hardware execution. No new APK is built or installed by this correction; the
old installed SO has not been established as corresponding to these source
bytes. Fresh Android source qualification, build and device tests remain needed.
Continuous identity stability, motion accuracy, performance and30FPS are open.
Unified-input Task11 stays open; no main merge or Release is authorized here.

The newly requested High/Medium/Low quality selector is a separate pending scope.
No quality option, new resolution or ModelPack is implemented in this correction.
