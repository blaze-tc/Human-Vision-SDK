# Ordinary skeleton objects — 2026-10-04

The formal unified Demo now draws its points with enabled MeshRenderers and
its bones with enabled LineRenderers through an ordinary dedicated Camera.
The prior disabled-renderer CommandBuffer mechanism is removed. A transparent
GPU render target is composed with the preview below the controls. These are
image-plane objects, not measured metric-depth joints. Existing source input,
native libraries, model weights, profiles and inference remain unchanged.

`SkeletonImagePlane` maps upright normalized joints through the actual preview
corners into the camera plane. Point/bone pools follow configured capacity,
reuse their objects and hide unavailable joints by GameObject activity. The
formal Canvas binds this overlayer to the same manager, frame source, regions
and per-mode style. Reference defaults remain 9px lines and 27px points;
resolution scaling follows the Canvas, rather than source video pixel count.
Only observed hand landmarks are drawn; this change does not add hand inference.

Reserve a free user layer, default 30, for the pooled objects. The dedicated
camera renders only that layer. The explicitly assigned foreground camera
temporarily excludes it; restoration retains application changes to all other
mask bits. Additional game cameras must exclude it themselves, or the renderer
reports an actionable error and releases its composition. Occupied layers are
rejected before allocating the pool. Scene/Preview cameras may inspect geometry.
No global camera-mask edits, TagManager edits or full-frame CPU readback are added.

## Verification

The isolated real Unity 2021.3.45f1 graphics fixture runs on Direct3D11 / NVIDIA
RTX 2060 without `-nographics`. Enabled-renderer and command-buffer regressions
first failed 2/14 tests. A mask-ownership regression then failed 1/22; the final
renderer passed 22/22 with actual bone alpha 1 and 1090 colored point pixels.

Import into the user's actual project exposed a test-fixture defect: its
untagged Presentation Camera was not isolated by Camera.main-only setup.
The retained actual job `060d036a` failed 7/14. Production guards remained
unchanged. Tests now temporarily snapshot/disable ordinary preexisting loaded
scene cameras and restore their exact enabled flags. A new representative
untagged-camera regression failed 1/23 before this fix; final isolated filtered
and unfiltered suites both pass 23/23. Hidden/DontSave camera discovery remains
outside this ordinary-camera fixture coverage.

The current `E:/UnityProject/Human-Vision-SDK-Test` imports immutable local
`out/input/production-correction/user-packages-v12`. Fresh actual-project tests:

| Filter | Job | Passed |
| --- | --- | ---: |
| HumanVisionSkeletonObjectTests | bf306e25 | 15/15 |
| HumanVisionSkeletonPlaneTests | d1a799ba | 8/8 |
| RuntimeProfileSelectionTests | cf4366a4 | 3/3 |

All 21 renderer source hashes and all 419 candidate package files were checked.
V12 differs from V11 only in the test fixture and asset hash index. Actual formal
video screenshots show upright colored bones and points aligned with people;
selected real point/line components have enabled=true. Runtime diagnostics
confirm windows-pc-directml / actual DirectML / hands disabled. The user's
four-region/capacity configuration and source settings were preserved.

A fresh 15-second sampled HUD run contains 60 records. Median native FPS is
25.051, local result-age P50/P95 (nearest rank) 73.373/96.041ms. The first two samples are
empty immediately after seeking; the following 58 render the composition.
These are local PC diagnostics for the supplied 25FPS video, not an exhaustive
observation stream or evidence of Android 30 fresh complete frames/s.
Post-test formal Play reports no Console errors. Independent frozen V3 and V4
spec compliance and code quality reviews both pass.

Canonical and UPM managed compilation passes with Windows and Android defines
across Input/Runtime/Demo/Editor/Tests assemblies; architecture/public-surface
checks pass. No native changes required rebuilding native targets. These checks
do not constitute Windows/Android Player or physical device acceptance.

Reproduction: invoke `test_run` with the above EditMode filters on the current
UnitySkills instance and poll `test_get_result`; formal playback uses the
supplied video-1.mp4 from its dance section at 37 seconds. Detailed raw receipts,
frozen candidate and independent reviews are retained in the local SDD ledger
`renderer-correction/v4` and `production-correction-user-integration-v12`.

## Remaining gates

Task1-10 implementation is verified; Task11 unified physical acceptance remains
open. The corrected runtime closures are local evaluation compositions, not a
newly published Git package. Android's existing build validator still assumes
the older two-component TopDown pack, so it must admit the already-approved
single-model YOLO contract with equally strict hash/profile checks before the
corrected formal Android Demo can be built. No new model or native optimization
is implied. Earlier invalid Task11 measurements remain invalid and retained.
Camera/Video/RTSP physical follow, sustained fresh complete observation FPS,
latency and final user acceptance remain outstanding. No main merge or Release.
