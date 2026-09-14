# 0002: Android runtime mode and additive GPU ABI

Date: 2026-09-14  
Status: accepted for Milestone A; implementation of the GPU bridge and ncnn runtime remains deferred to Milestones B and C.

## Decision

Android builds select one explicit, baked runtime mode before the player is
built:

| Mode | Baked profile | Frame path |
|---|---|---|
| `android-ncnn-vulkan` | `android-ncnn-vulkan` | Planned Vulkan GPU bridge and ncnn Vulkan path |
| `android-ort-xnnpack` | `android-ort-xnnpack` | Existing V1 CPU-frame path |
| `android-ort-cpu` | `android-ort-cpu` | Existing V1 CPU-frame path |

`android-ncnn-vulkan` is the default when no setting has been saved. It is not
an automatic selection mode. On Android an empty or `auto` profile resolves to
the baked profile; an explicit profile must equal that baked profile. A conflict
fails with both IDs in the error. Every approved Android profile selects one
pipeline and one backend with `allow_fallback: false`. Missing NCNN requirements
fail initialization/build validation; they never substitute an ORT profile.

The stage intentionally rejects an NCNN production build until Milestone C
installs `precision-t-26-ncnn-fp16` and its SHA-256 index. The EditMode build
validator proves that, with every other NCNN prerequisite present, the only
errors are `HasNcnnModelPackAssets` and `HasNcnnModelPackSha256Index`, with the
actionable Milestone C message. ORT modes remain independently buildable under
their own explicit selection.

## Compatibility and ownership

V1 stays byte-for-byte stable. `HV_VideoFrame`, `HV_RuntimeSubmit`, V1 public
C layouts/functions, V1 plugin/backend/pipeline tables, canonical snapshots, and
the Unity skeleton API retain their contracts. The A4 comparison of the V1
public headers (`humanvision_c.h`, `humanvision_types.h`,
`humanvision_plugin.h`, and `humanvision_v2.h`) from A3 base `adb2829` to A4
commit `32066b5` is empty. The A4 full native suite includes the V1 ABI/runtime
snapshots; A5 reran that suite successfully.

The additive V2 tables own opaque GPU frame references, GPU pipeline entry
points, GPU backend sessions, and Host Services V2. The GPU backend owns AHB
imports, ncnn allocators/sessions, preprocessing, tensor inference, output
transfer, and backend diagnostics. The TopDown/RTMO pipeline owns model
orchestration, decoding, NMS, ROI construction, and source-schema observations.
Common services own region assignment, canonical mapping, tracking, temporal
sampling, and public snapshots. Unity only submits an opaque frame through the
additive bridge and renders semantic results. V2 must not reinterpret a V1 table,
and V1 CPU profiles continue through their existing path.

## Evidence and limitations

Milestone A was verified at `32066b565da85877f16931ab2532a749d846af1c` on
branch `codex/android-ncnn-vulkan-implementation`. The full native suite, Unity
EditMode suite, architecture/documentation guard, component-catalog check, and
package dry run are recorded in `docs/DEVELOPMENT_STATUS.md`.

This decision does not claim a Vulkan/AHardwareBuffer implementation, ncnn
linking, GPU camera copy, model conversion, inference, Android package execution,
or device performance. Hardware, camera/RTSP, 1/2/4/6/8-person FPS, latency,
accuracy, thermals, and skeleton quality remain user acceptance after the
corresponding B/C/D milestones.
