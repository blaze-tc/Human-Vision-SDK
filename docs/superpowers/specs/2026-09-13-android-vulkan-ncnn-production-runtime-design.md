# Android Vulkan + ncnn Production Runtime Design

Status: proposed for user review; implementation is not authorized yet  
Date: 2026-09-13  
Baseline: `origin/main` at `f283a5e2ce7a5afb46aa7902fcc03ac44957074c`  
Target device: Snapdragon 888 / Adreno 660, Android 14, ARM64  

## 1. Decision summary

The next Android production runtime uses a GPU-native camera path and a strict,
explicit runtime-mode selection:

| Unity Android Runtime Mode | Runtime profile | Behavior |
|---|---|---|
| `NCNN Vulkan` | `android-ncnn-vulkan` | Default. Requires Vulkan, the GPU Frame Bridge, ncnn Vulkan, and every FP16 capability declared by the profile. Any missing requirement fails initialization with an actionable error. |
| `ORT XNNPACK` | `android-ort-xnnpack` | Compatibility mode selected by the user before building. Uses the existing CPU-frame input and does not select ncnn. |
| `ORT CPU` | `android-ort-cpu` | Compatibility mode selected by the user before building. Uses the existing CPU-frame input and does not select ncnn. |
| `MediaPipe` | reserved | Future registry entry. No MediaPipe dependency or implementation is included in this stage. |

There is no automatic fallback between modes. The selected mode is baked into the
Android player and is authoritative at runtime. All three implemented backends may
remain in the same APK during this stage; build-time dependency stripping is deferred.

The first production pipeline is RTMDet Nano 320x320 plus RTMPose-t TopDown
192x256. The second pipeline is RTMO for the 3-8-person path. The existing Unity
skeleton API, V1 C ABI, Canonical Skeleton, Tracker, Profile, ModelPack, and result
snapshot contracts remain stable.

The camera frame is never returned to CPU memory in `NCNN Vulkan` mode. Small model
output tensors may be read to CPU for RTMDet decoding/NMS, SimCC decoding, canonical
mapping, region assignment, tracking, and snapshot publication.

## 2. Scope and exclusions

### Included

- Unity 2021.3.45f1 Android ARM64 player support.
- Vulkan as the default graphics API for `NCNN Vulkan` builds.
- A Unity Native Vulkan GPU Frame Bridge based on Unity's public native-plugin APIs.
- A fixed, reusable Android Hardware Buffer exchange ring between Unity Vulkan and
  the ncnn Vulkan device.
- Tencent ncnn Vulkan/FP16 backend built from pinned official source.
- RTMDet Nano plus RTMPose-t Body26 TopDown model conversion, GPU preprocessing,
  inference, decoding, canonical mapping, and tracking.
- RTMO Vulkan/FP16 integration after TopDown acceptance.
- Post-inference Region assignment using pelvis, with bbox-center fallback.
- SDK-level Unity Project Settings, build-time validation, runtime capability checks,
  diagnostics, automated non-hardware tests, Android build checks, and APK inspection.
- A package that the user can build and test on Snapdragon 888 directly; no separate
  temporary benchmark application or benchmark-only pipeline is introduced.

### Excluded

- Further ORT, NNAPI, XNNPACK, CPU preprocessing, or CPU Frame Bridge optimization.
- Renderer changes, Hand/Handtip/Thumb work, QNN, or Windows runtime work.
- MediaPipe code or dependency integration.
- Native Camera2 replacement of Unity's camera source.
- APK dependency stripping by runtime mode.
- AzureKinectExamples integration.

The ORT modes remain buildable compatibility paths. Their existing behavior is
preserved except for profile renaming/mapping and the shared Android build baseline.

## 3. Approaches considered

### Recommended: Android Hardware Buffer exchange ring

Unity and ncnn keep separate Vulkan logical devices on the same physical Adreno GPU.
The bridge owns three reusable RGBA Android Hardware Buffers. Each buffer is imported
as a Vulkan image by Unity and by ncnn. Unity performs a GPU-to-GPU blit from the
oriented camera RenderTexture into a free buffer, exports a synchronization fence,
and returns immediately. The native inference worker waits for that fence and imports
the same buffer with ncnn's official Android Hardware Buffer API.

Benefits:

- No full-frame CPU readback or CPU-to-GPU upload.
- ncnn keeps its official Vulkan device, allocator, extractor, and queue behavior.
- Detector decode can happen on the native worker, followed immediately by pose ROI
  inference without waiting for another Unity render frame.
- The bridge has explicit ownership, synchronization, backpressure, and teardown.
- ncnn source can remain unmodified.

Costs:

- Android API 26 is required for this first multi-backend package.
- Unity's Vulkan device must enable Android Hardware Buffer and external-semaphore
  extensions during device creation.
- The implementation must handle Vulkan external-memory ownership and sync-fd rules
  precisely.

### Rejected: make ncnn adopt Unity's Vulkan device

Official ncnn constructs and owns its own `VkInstance`, `VkDevice`, queues, allocators,
and command buffers. It does not expose a supported constructor for an existing Unity
device. Making this work would require a downstream ncnn device/queue/lifecycle fork,
plus asynchronous submission changes so inference does not block Unity's render
thread. This creates a large long-term patch surface and couples the backend to Unity.

### Rejected: native Camera2 source or reduced CPU readback

A Camera2/AImageReader source could feed ncnn through Android Hardware Buffer, but it
would replace the requested Unity Texture source and duplicate camera ownership.
Reading back a resized tensor instead of the whole camera frame reduces traffic but
still breaks the required GPU-to-ncnn chain. Neither is the production architecture.

## 4. System architecture

```text
Unity WebCamTexture / RTSP Texture
                |
       existing orientation RenderTexture
                |
   HumanVisionAndroidGpuFrameSource (C#)
                |
 GL.IssuePluginEventAndData + native-owned event slot
                |
===============================================================
 libhumanvision.so / Unity Native Vulkan GPU Frame Bridge
                |
 IUnityGraphicsVulkanV2::AccessTexture
                |
 IUnityGraphicsVulkanV2::AccessQueue(flush=true)
                |
 GPU blit -> reusable RGBA AHardwareBuffer + sync fd
                |
        latest-ready frame slot
                |
===============================================================
 Runtime Host GPU worker
                |
 pipeline.topdown -> backend.ncnn.vulkan
                |
 AHB import -> GPU resize/normalize -> RTMDet Nano
                |
 small detector output -> CPU decode/NMS
                |
 GPU ROI crop/normalize -> RTMPose-t for selected boxes
                |
 small SimCC outputs -> CPU decode
                |
 Canonical Skeleton mapping -> Region assignment -> Tracker
                |
 stable HV_CanonicalBodyV1 snapshot
                |
 existing Unity HumanVisionManager public API and overlay
```

The bridge, GPU backend, algorithm pipeline, and common services remain separate
components. The Runtime Host selects them from the profile and contains no model-name
or ncnn-specific branches.

## 5. Runtime mode and Unity Project Settings

### 5.1 Stored setting

Add `Project/Human Vision/Android Runtime` to Unity Project Settings. Store a stable
string mode ID in `ProjectSettings/HumanVisionAndroidRuntimeSettings.asset` through an
Editor `ScriptableSingleton`:

```text
android-ncnn-vulkan
android-ort-xnnpack
android-ort-cpu
```

The UI displays friendly names, the mapped profile, required graphics API, required
minimum Android API, packaged backend status, and the most recent validation result.
The internal registry is data-driven so a future `android-mediapipe` entry does not
change the existing public skeleton API.

`android-ncnn-vulkan` is the default for a new or upgraded project that has no saved
mode. The settings UI offers explicit `Apply recommended Vulkan settings` and
`Validate current project` actions. Validation never silently changes the selected
runtime mode.

### 5.2 Build-time bake and runtime resolution

The Android build processor writes the selected mode and exact profile ID into the
generated Android application manifest as Human Vision metadata. It also writes the
mode into the SDK build report. Project Settings files themselves are not read by the
player.

At runtime the Unity SDK reads the baked metadata once during initialization and maps
it to the profile. `HumanVisionConfig.Profile` remains source-compatible. On Android:

1. An empty or `auto` profile resolves to the baked Project Settings profile.
2. An explicit Android profile is accepted only if it equals the baked profile.
3. A conflicting explicit profile fails with both IDs in the error message.
4. There is no backend fallback inside a resolved profile.

This prevents a scene or prefab from silently overriding the mode selected for the
APK. Non-Android profile behavior remains unchanged.

The Demo Scene removes its benchmark-provider selector and reports the SDK-level mode
as read-only diagnostics. Gameplay continues to call the same `HumanVisionManager`
and camera APIs.

### 5.3 Profile definitions

`android-ncnn-vulkan` declares:

- `pipeline.topdown` with the ncnn FP16 TopDown ModelPack first.
- `backend.ncnn.vulkan` as the only backend.
- `allow_fallback: false`.
- requirements for `gpu_input`, Vulkan, Android Hardware Buffer, external sync, and
  the exact FP16 features used by the converted models.
- hands disabled.
- `body_fps: 30`, `output.hz: 60`.
- TopDown capacity limited to the accepted capacity until RTMO is added.
- RTMO selection for capacities 3-8 after the RTMO milestone passes.

`android-ort-xnnpack` and `android-ort-cpu` each select exactly one existing backend,
set `allow_fallback: false`, use the CPU-frame bridge, and keep hands disabled. Legacy
diagnostic profile IDs remain readable for compatibility but are not selectable from
the new SDK setting.

## 6. Stable and additive ABI design

### 6.1 Contracts that do not change

- `HV_VideoFrame`, `HV_RuntimeSubmit`, and all V1 function signatures/layouts.
- `HV_CanonicalBodyV1`, its 32-joint IDs, timestamps, region fields, and snapshots.
- Existing Unity body/joint/result APIs.
- Existing V1 pipeline and backend callback tables.

The ORT modes continue to use these V1 CPU-frame contracts.

### 6.2 Additive public native bridge API

Add a versioned Android GPU-frame descriptor and bridge functions without modifying
existing structs:

```c
typedef struct HV_AndroidGpuFrameV1 {
    uint32_t struct_size;
    uint32_t api_version;
    void* unity_native_texture;
    int32_t width;
    int32_t height;
    int64_t frame_id;
    int64_t timestamp_us;
    uint32_t orientation;
    uint32_t flags;
} HV_AndroidGpuFrameV1;

HV_Result HV_RuntimePrepareAndroidGpuFrame(
    HV_RuntimeHandle runtime,
    const HV_AndroidGpuFrameV1* frame,
    void** render_event_data);

void* HV_GetAndroidGpuFrameRenderEventFunc(void);
HV_Result HV_RuntimeGetAndroidGpuBridgeStatus(...);
```

The names may be normalized during the implementation plan, but the semantics are
fixed:

- `Prepare` copies metadata into a native-owned fixed event slot; C# does not allocate
  unmanaged frame data per submission.
- `render_event_data` remains valid until the render callback consumes or invalidates
  it.
- stale callbacks carry a session generation and become safe no-ops after shutdown.
- submission remains non-blocking on the Unity main thread.
- the GPU bridge maintains latest-frame-wins counters distinct from inference drops.

### 6.3 Additive plugin ABI

Keep `HV_PluginApiV1`, `HV_PipelineApiV1`, and `HV_BackendApiV1` byte-for-byte stable.
Add V2 query/extension tables for GPU image processing:

- `HV_PluginApiV2` has the V1 identity/capability prefix and optional GPU tables.
- `HV_GpuFrameRefV1` is an opaque, reference-counted image plus geometry, frame ID,
  timestamp, and synchronization token. It exposes no Unity type to pipelines.
- `HV_GpuPipelineApiV1` accepts `HV_GpuFrameRefV1` and emits the existing generic
  `HV_ObservationFrameV1`.
- `HV_GpuBackendApiV1` creates model sessions and performs generic image-transform to
  tensor inference. It returns raw named tensor outputs and never decodes skeletons.
- `HV_HostServicesV2` creates/releases GPU backend sessions selected by the profile.

The ncnn backend owns AHB import, ncnn allocators, model sessions, GPU preprocessing,
FP16 inference, output transfer, and backend diagnostics. The TopDown and RTMO
pipelines own model orchestration, detector/pose decoding, NMS, ROI construction, and
mapping observations into their declared source schemas. Common services own Region,
Canonical Skeleton, Tracker, temporal sampling, and public snapshots.

Static and dynamic plugin discovery both validate `struct_size`, `api_version`,
capabilities, callback presence, and profile requirements before creating a session.

## 7. Vulkan GPU Frame Bridge

### 7.1 Unity render event flow

`HumanVisionLiveSource` continues to produce the correctly oriented RenderTexture.
In `NCNN Vulkan` mode `VideoPlayerFrameSource.SubmitExternalTexture` routes to a new
GPU source instead of `AsyncGPUReadback`.

For each accepted frame:

1. C# requests a native event slot with texture pointer, dimensions, frame ID, and
   monotonic timestamp.
2. C# calls `GL.IssuePluginEventAndData` with the stable native pointer.
3. The render callback verifies Vulkan and calls
   `IUnityGraphicsVulkanV2::AccessTexture` with a transfer-read layout.
4. It calls `IUnityGraphicsVulkanV2::AccessQueue(..., flush=true)`. Unity submits all
   prior work before granting exclusive queue access.
5. The queue callback records/submits a GPU blit into a free AHB slot and signals an
   Android sync fd. It does not wait for inference.
6. The worker consumes the newest ready slot. Older ready slots are released once
   their copy fence is safe and counted as dropped.

The bridge does not call `AsyncGPUReadback`, `Texture2D.ReadPixels`, `GetPixels`, or
copy a full camera image through JNI/PInvoke.

### 7.2 AHB ring and ownership

Use three fixed slots created at startup or geometry change. Each slot contains:

- one `AHardwareBuffer` in `AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM`;
- Unity-device imported image/view/memory;
- ncnn-device import state created by official ncnn APIs;
- reusable Unity copy command resources;
- frame metadata and generation;
- copy fence/sync fd and state.

States are `Free -> UnityCopyQueued -> ReadyForNcnn -> InferenceRunning -> Free`.
No state permits Unity writes while ncnn reads. A geometry/orientation change creates
a new generation only after active slots drain; stale render events are discarded.

The bridge records one GPU scaling blit into the configured analysis geometry. The
existing source-orientation RenderTexture remains the single coordinate-space source
for preview, inference, and overlay. This keeps dynamic portrait/landscape rotation,
front-camera mirror, skeleton coordinates, and Region coordinates aligned.

### 7.3 Vulkan initialization and teardown

The Android native plugin is configured to preload. During Vulkan initialization it
uses `IUnityGraphicsVulkanV2` interception to observe the physical device and enable
only supported extensions needed by AHB import and sync fd. Missing extensions do not
trigger another backend; they make the selected mode unavailable with a precise error.

The first implementation package requires Android API 26 for every runtime mode
because all backends are shipped together and ncnn AHB support is compiled at API 26.
Keeping ORT APKs at API 24 is deferred to runtime-mode dependency stripping/separate
native binaries.

Shutdown order is:

1. reject new GPU submissions;
2. invalidate the session generation;
3. drain or cancel native event slots;
4. wait only on native control teardown, never inside a per-frame Unity call;
5. complete ncnn work and release imported images;
6. destroy AHB slots before the Unity Vulkan device shutdown callback;
7. destroy ncnn sessions/device state.

Android pause/resume and graphics-device recreation follow the same teardown and
recreate path. A stale texture pointer is never reused after a device event.

## 8. ncnn Vulkan/FP16 backend

### 8.1 Dependency policy

Pin Tencent ncnn official release tag `20260526`. That source contains
`VkAndroidHardwareBufferImageAllocator` and
`VkImageMat::from_android_hardware_buffer`. Official prebuilt Android archives are
built for an API level that removes the API-26 AHB symbols, so Human Vision builds the
unmodified official source with:

```text
ANDROID_ABI=arm64-v8a
ANDROID_PLATFORM=android-26
NCNN_VULKAN=ON
NCNN_SHARED_LIB=OFF
NCNN_BUILD_TOOLS=OFF
NCNN_BUILD_EXAMPLES=OFF
NCNN_BUILD_BENCHMARK=OFF
```

The exact upstream archive hash, source commit, CMake options, build tool versions,
ncnn BSD-3-Clause license, and produced library hash are recorded in third-party and
release manifests. No floating `master` dependency is allowed.

### 8.2 Backend sessions

`backend.ncnn.vulkan` creates one immutable ncnn `Net` per model role and reuses:

- `VulkanDevice` and pipeline cache;
- blob/workspace/staging Vulkan allocators;
- extractors per job;
- imported frame image state per AHB slot;
- detector and pose input/output buffers;
- decoded output storage sized from the ModelPack contract.

The backend enables only FP16 features required by the profile. The profile names the
required level explicitly rather than using an ambiguous `fp16=true`:

- FP16 packed storage;
- FP16 storage buffers;
- FP16 arithmetic when the validated model supports it.

Startup fails if any required ncnn/Vulkan capability is missing. Diagnostics report
requested and actual storage/arithmetic modes; a lower precision mode is never chosen
silently.

### 8.3 GPU preprocessing and CPU output boundary

ncnn imports the AHB as a `VkImageMat`, converts RGBA to model RGB ordering, and runs
resize/letterbox or affine ROI crop, normalization, packing, and type conversion on
the GPU. Detector preprocessing uses the whole analysis image. Pose preprocessing
uses the detector bbox transforms against the same retained image slot.

Only raw detector head outputs/final detection tensors and pose SimCC tensors cross
to CPU. Their shapes and maximum byte counts are declared in the ModelPack and checked
before inference. The backend cannot return arbitrary-size buffers supplied by a
model file.

## 9. Model conversion and ModelPacks

### 9.1 RTMDet Nano

Source checkpoint:

`rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth`

OpenMMLab currently marks RTMDet's MMDeploy ncnn combination unsupported, so the
implementation must not describe RTMDet Nano as an officially validated ncnn model.
The production conversion path is:

1. pin the official MMPose/MMDetection/MMDeploy source revisions used by the existing
   person-only checkpoint;
2. export a static 320x320 raw RTMDet Nano graph without backend-specific NMS;
3. convert through official pnnx/ncnn tooling;
4. replace unsupported export patterns only with documented pnnx graph passes or
   narrowly scoped ncnn custom layers;
5. run `ncnnoptimize` with the chosen FP16 weight setting;
6. implement RTMDet decode, confidence filtering, person-only selection, and NMS in
   `pipeline.topdown`, not in the backend;
7. compare boxes/scores against the pinned PyTorch or current ORT reference on a fixed
   golden corpus before Android integration.

If conversion or parity fails, the milestone remains failed. It does not substitute a
different detector, use fake detections, or route the production profile through ORT.

### 9.2 RTMPose TopDown

Use the existing official RTMPose-t Body26 checkpoint and OpenMMLab's validated
`pose-detection_simcc_ncnn-fp16_static-256x192` deployment pattern. Preserve the
192x256 model input orientation used by the current ModelPack. Validate SimCC output
names/shapes, affine transform reversal, confidence, Body26 mapping, and derived
Canonical Skeleton joints against the current reference.

### 9.3 RTMO

RTMO work starts only after the TopDown player produces real camera skeletons through
the new bridge/backend. It receives a separate ncnn FP16 ModelPack and stays in
`pipeline.rtmo`. The profile switches capacities 3-8 to RTMO only after conversion,
golden parity, Android build, and physical-device acceptance pass.

### 9.4 ModelPack schema

Preserve schema 1 readers. Add schema 2 for multi-file ncnn models:

```json
{
  "schema_version": 2,
  "pack_id": "precision-t-26-ncnn-fp16",
  "pipeline_id": "pipeline.topdown",
  "capabilities": ["body_pose", "multi_person", "gpu_input", "vulkan", "fp16"],
  "models": [
    {
      "role": "detector",
      "format": "ncnn",
      "assets": {"param": "detector.ncnn.param", "bin": "detector.ncnn.bin"},
      "input_contract": {"width": 320, "height": 320},
      "output_contract": {"decoder": "rtmdet_nano_raw_v1"}
    }
  ]
}
```

Every asset has a SHA-256 entry, provenance, license, input/output contract, dtype,
normalization, color order, and conversion recipe. Runtime loading is confined to the
pack root and rejects missing, changed, or incompatible assets.

## 10. TopDown data flow

For each newest accepted GPU frame:

1. Wait for the Unity-to-AHB sync fd on the native inference worker.
2. Import/reference the AHB with ncnn; retain the slot until all pose work finishes.
3. GPU resize/letterbox/normalize to 320x320.
4. Run RTMDet Nano with ncnn Vulkan/FP16.
5. Read only detector outputs; decode and NMS on CPU.
6. Select at most `MaxBodies` person boxes by the pipeline's declared ordering.
7. Build affine transforms for all selected boxes.
8. GPU crop/resize/normalize each ROI from the retained AHB.
9. Run RTMPose-t on each ROI through the same ncnn backend and allocator pool.
10. Read SimCC outputs; decode source joints and reverse affine transforms.
11. Map Body26 into `HV_ObservationFrameV1`/Canonical Skeleton and derive pelvis.
12. Assign Regions from inference results.
13. Track identities and publish one coherent result snapshot with original frame ID
    and timestamp.
14. Release the AHB slot.

There is at most one inference job running and one newest pending ready frame. A new
ready frame replaces the older pending frame. Frames already running are never
relabeled with a newer timestamp. This keeps latency bounded and preserves source
metadata truth.

## 11. Region assignment after inference

The `NCNN Vulkan` path never masks the source image. `MaskRegions` is bypassed for GPU
profiles and remains only for legacy CPU behavior until separately retired.

Common Region service assigns each inferred body as follows:

1. derive pelvis from valid left/right hip joints when the source schema does not
   provide it;
2. use the valid pelvis point for containment;
3. use bbox center only when pelvis is unavailable;
4. discard a body outside all configured Regions;
5. allow at most one body per Region;
6. for collisions, preserve an existing track already assigned to that Region when
   association is valid, then prefer higher pose confidence, higher detector
   confidence, and stable source order;
7. pass the assigned Region index into Tracker, which prohibits cross-Region identity
   swaps while Regions are active.

With no configured Regions, all bodies remain eligible and display indices follow the
existing tracker behavior. Region revision, index, frame ID, and observation timestamp
continue through the existing public snapshot fields.

## 12. Validation and failure behavior

### 12.1 Build-time validation

For every Android build:

- IL2CPP and ARM64-only are required.
- Android minimum API 26 is required during the non-stripped multi-backend stage.
- the selected mode and profile must exist and match exactly;
- runtime index, profile, ModelPack manifests, assets, and SHA-256 records must agree;
- required native libraries and Unity PluginImporter platform/CPU settings must pass;
- the build report records the selected mode and packaged backend IDs.

Additional `NCNN Vulkan` blockers:

- Auto Graphics API is disabled;
- Vulkan is first in Android Graphics APIs;
- the native GPU bridge is enabled for Android ARM64 and preloaded;
- `android-ncnn-vulkan`, ncnn ModelPack, ncnn license/provenance, and generated models
  are present;
- architecture/package checks find required bridge and plugin ABI symbols;
- the profile has exactly `backend.ncnn.vulkan` and `allow_fallback: false`;
- the profile declares the required GPU/FP16 capabilities.

OpenGLES may remain later in Unity's graphics API list during this stage, but runtime
initialization fails if Unity actually starts with it. The settings UI warns that
removing OpenGLES gives a stricter production package.

ORT mode builds do not require Vulkan-first, but still validate their single backend,
profile, models, and CPU frame source.

### 12.2 Runtime checks for `NCNN Vulkan`

Initialization performs these checks before accepting a camera frame:

- Android API level and ARM64 process;
- actual Unity graphics API is Vulkan;
- `IUnityGraphicsVulkanV2` and render-event bridge are available;
- Unity and ncnn physical-device identity matches;
- required AHB, foreign-memory, sync-fd, and image-format capabilities;
- successful creation/import of every reusable AHB slot;
- ncnn Vulkan device is valid and not blacklisted;
- required FP16 storage/arithmetic features;
- profile/backend/pipeline/ModelPack capability match;
- model assets and hashes;
- a bounded GPU bridge initialization self-check.

Any failure returns a stable error code plus a message naming the mode, failed
requirement, observed value, and corrective Project Settings action. Examples:

```text
android-ncnn-vulkan initialization failed: Unity is running OpenGLES3; rebuild with Vulkan first.
android-ncnn-vulkan initialization failed: Adreno driver lacks required FP16 storageBuffer16BitAccess.
android-ncnn-vulkan initialization failed: detector.ncnn.bin SHA-256 does not match its ModelPack.
```

No error path creates an ORT session.

### 12.3 Diagnostics

Add diagnostics without changing gameplay APIs:

- baked runtime mode and resolved profile;
- actual Unity graphics API;
- Unity/ncnn GPU vendor, device, and driver identity;
- AHB/sync/FP16 capability decisions;
- bridge slot states, accepted frames, busy drops, replaced pending frames, and errors;
- detector, pose-per-person, total inference, decode, Region, and publication timings;
- source frame ID/timestamp, result age, fresh body FPS, and output sampling FPS;
- requested/actual ncnn precision flags.

Diagnostics must distinguish preview FPS, GPU capture FPS, fresh inference FPS, and
sampled output FPS.

## 13. Testing and acceptance

### 13.1 Automated tests before device testing

Follow red-green development for every contract change.

Native non-hardware tests:

- V1 ABI byte/layout and behavior regression tests;
- V2 struct size/version/capability validation;
- native event-slot generation and stale-callback safety;
- AHB ring state transitions, latest-frame replacement, drop accounting, and teardown
  using a fake GPU bridge;
- strict no-fallback profile selection and error propagation;
- ncnn backend selection only when all declared capabilities match;
- TopDown orchestration with fake GPU backend tensors;
- pelvis-first/bbox-fallback Region assignment, outside filtering, collisions, revision,
  and tracker stability;
- snapshot frame/timestamp truth under dropped frames.

Model tests:

- reproducible converter commands and hashes;
- RTMDet Nano graph/operator audit;
- RTMDet box/score/NMS golden parity;
- RTMPose SimCC and affine-reversal golden parity;
- FP32 reference versus ncnn FP16 tolerance with documented thresholds;
- invalid or oversized output contracts fail before buffer access.

Unity EditMode tests:

- mode registry/default/mapping and forward-compatible IDs;
- Project Settings serialization;
- Android manifest metadata generation;
- profile conflict rejection;
- every build validation rule with passing/failing fixtures;
- GPU-mode routing never calls the AsyncGPUReadback submission path;
- ORT modes continue to route through the existing CPU frame source.

Build/package checks:

- Android ARM64 native build with pinned NDK/API 26 and ncnn Vulkan;
- Unity managed/runtime/demo compilation;
- architecture dependency guards;
- UPM isolation/import tests;
- APK inspection for ARM64 libraries, manifest mode, profiles, model assets, hashes,
  and licenses;
- exported symbols and no accidental MediaPipe/QNN dependency.

### 13.2 Snapdragon 888 physical acceptance

The user owns final physical-device acceptance. The SDK provides an integrated camera
scene and persistent diagnostics; it does not provide a separate benchmark-only app.

TopDown gate, capacities 1 and 2:

- real camera preview remains smooth and correctly oriented in portrait/landscape;
- skeleton follows the same current image without flashing or multi-second stalls;
- 30 fresh complete skeleton results per second at the accepted capacity after warmup;
- bounded latest-frame queue with no monotonic growth in result age;
- target steady-state source age: p95 at or below 100 ms, with no sample above 250 ms
  outside lifecycle transitions;
- stable track IDs and Region slots during normal motion/crossing;
- no full-frame CPU readback counter or allocation in diagnostics;
- no Vulkan validation, native crash, ANR, or device-loss loop;
- a 15-minute thermal run reports timing/frequency changes without changing backend.

RTMO gate, capacities 3, 4, 6, and 8:

- the same freshness, correctness, identity, Region, crash, and thermal criteria;
- 30 fresh complete skeleton results per person per second at the configured capacity.

If a gate fails, the corresponding milestone remains open. The SDK does not inflate
output FPS through sampling, hold stale poses without their source timestamps, or
claim a lower-capacity result as 8-person acceptance.

## 14. Delivery sequence

Each milestone writes the failing acceptance test first, implements the minimum
production change, builds affected targets, runs focused/full checks, updates
`docs/DEVELOPMENT_STATUS.md` with exact evidence, and commits only verified changes.

### Milestone A: runtime-mode contracts and strict selection

- Project Settings registry and persistence.
- three profile IDs and strict mapping.
- build metadata and validation.
- additive GPU ABI/plugin ABI contracts with fake implementations.
- no camera or inference implementation yet.

### Milestone B: Vulkan AHB bridge and ncnn backend foundation

- Unity Vulkan preload/device integration.
- reusable AHB ring, sync fd, lifecycle, latest-frame scheduling.
- pinned official ncnn source build and generic Vulkan/FP16 backend.
- GPU image import/preprocessing and model-session tests.
- integrated Android player route; no temporary benchmark app.

### Milestone C: RTMDet Nano + RTMPose TopDown production pipeline

- reproducible model conversion and golden parity.
- detector decode/NMS and pose orchestration.
- Canonical Skeleton, post-inference Region assignment, Tracker, diagnostics.
- complete Snapdragon 888 APK for user acceptance at capacities 1 and 2.

### Milestone D: RTMO production pipeline

- RTMO ncnn conversion/parity and Vulkan execution.
- profile capacity routing for 3-8 people.
- complete Snapdragon 888 APK for user acceptance at 3/4/6/8 people.

### Milestone E: package and release candidate

- maintenance docs, source/licensing manifests, package checks, UPM import checks,
  Android build evidence, and user device results.
- no dependency stripping yet.
- release publication only after the user explicitly accepts the device results and
  the repository release checklist passes.

## 15. Documentation updates during implementation

Implementation must update:

- `docs/ARCHITECTURE.md` for GPU frame and V2 extension boundaries;
- `docs/SDK_API.md` for additive bridge contracts and unchanged Unity API;
- `docs/MODEL_MANIFEST.md` for schema 2 ncnn assets/capabilities;
- `docs/TOOLCHAIN.md` for pinned ncnn, NDK, Vulkan, converters, and hashes;
- `docs/maintenance/CHANGE_MAP.md`, `PLUGIN_DEVELOPMENT.md`,
  `MODEL_PACK_GUIDE.md`, `PROFILE_GUIDE.md`, and `RELEASE_GUIDE.md`;
- Android camera/setup documentation for Runtime Mode selection and error recovery;
- `docs/DEVELOPMENT_STATUS.md` after every milestone with exact command results and
  explicit physical-device acceptance status.

## 16. Source evidence informing this design

- Unity 2021.3 local `IUnityGraphicsVulkan.h`: `AccessTexture`, `AccessQueue`, render
  event configuration, device callbacks, and Vulkan resource lifecycle contracts.
- Tencent ncnn official release `20260526` and source APIs for Vulkan, FP16, Android
  Hardware Buffer import, allocators, and extractors:
  <https://github.com/Tencent/ncnn/releases/tag/20260526>
- Tencent ncnn Android Hardware Buffer documentation:
  <https://github.com/Tencent/ncnn/wiki/use-ncnn-with-android-hardware-buffer>
- OpenMMLab RTMPose deployment documentation and ncnn FP16 configuration:
  <https://github.com/open-mmlab/mmpose/blob/main/docs/en/user_guides/how_to_deploy.md>
- OpenMMLab MMDeploy support tables: RTMPose lists ncnn support; RTMDet currently does
  not, which is why RTMDet conversion/parity is an explicit gate:
  <https://github.com/open-mmlab/mmdeploy/blob/main/docs/en/04-supported-codebases/mmpose.md>
  <https://github.com/open-mmlab/mmdeploy/blob/main/docs/en/04-supported-codebases/mmdet.md>
- MediaPipeUnityPlugin is used only as a reference for Unity/native GPU-frame lifecycle
  and pooling concepts. Its current shared-texture sample path is OpenGLES-specific and
  it is not a dependency:
  <https://github.com/homuler/MediaPipeUnityPlugin>
