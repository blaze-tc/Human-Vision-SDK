# Human-Vision-SDK 0.4 Real-Time Multi-Person Architecture Design

**Status:** Design approved in principle; implementation plan pending user review  
**Date:** 2026-09-10  
**Repository:** `blaze-tc/Human-Vision-SDK`  
**Target release:** `0.4.0-preview.1`

## 1. Goal

Human-Vision-SDK 0.4 replaces the current Android-oriented `RTMDet 640 + RTMPose-S WholeBody 133` execution path with a real-time multi-person architecture designed for Unity games.

The implementation must prioritize:

- Windows and Android support from one public SDK/API surface.
- 1–8 concurrent people.
- Stable person IDs and region assignment.
- Low-latency body skeletons suitable for running, jumping, crouching, reaching and collision/gameplay logic.
- Real hand-derived `Hand`, `HandTip`, and `Thumb` values when hand inference is available.
- Kinect/Azure-Kinect-style game-facing skeleton schema independent of the underlying model.
- Unity render/game loop staying responsive even when inference is slower.
- WebCam, RTSP and existing video input compatibility.
- Future RK3588/RKNN backend without changing the Unity API.
- Commercially practical open-source dependencies/licensing.

This release is not allowed to equate successful compilation with real-device performance acceptance.

## 2. Responsibility and acceptance boundary

### Codex responsibility

Codex is responsible for:

1. Implementing the architecture and code changes.
2. Adding/updating models, native libraries, package manifests and build scripts needed by the release.
3. Running compile, static, packaging and non-hardware tests that are safe in the development environment.
4. Producing Windows x64 and Android ARM64 deliverables where the repository build environment permits.
5. Updating developer/user documentation.
6. Publishing the completed source and release artifacts to the authorized GitHub repository.
7. Clearly recording what was verified automatically and what still requires physical-device validation.

### User responsibility

The user manually performs:

- Android phone tests.
- Camera tests.
- RTSP field tests.
- Multi-person 1/2/4/6/8-person tests.
- Accuracy, stability, latency, heat and sustained-performance acceptance.
- Final subjective gameplay acceptance.

Codex must not claim Android runtime FPS, accuracy or latency passes unless the user reports those results.

## 3. Evidence from the current implementation

The current 0.3.x line has several structural Android bottlenecks:

- Android detector uses ONNX Runtime CPU instead of a Snapdragon-specific accelerator path.
- Detector input is 640x640 FP32.
- The active pose model is `rtmpose_s_133.onnx`, a 133-landmark whole-body model.
- Top-down pose inference executes once per tracked person and therefore scales approximately with person count.
- Detector and pose preprocessing contain CPU-side image transform/normalization work.
- Existing device evidence on OnePlus 9 Pro / Snapdragon 888 recorded detector latency in the hundreds of milliseconds and historically around one second.
- Camera-only playback is smooth while SDK analysis is choppy, so camera presentation is not the primary root cause.

Existing strengths that must be retained:

- Latest-frame replacement rather than an unbounded queue.
- Native worker processing separated from Unity's main thread.
- Region configuration and assignment.
- RTSP/WebCam abstractions.
- Result snapshots and C ABI boundary.
- Existing package/build automation.

## 4. Open-source research conclusion

### 4.1 RTMO

RTMO is the preferred multi-person body pipeline because it is a one-stage multi-person pose estimator. It avoids the `detector + N pose inferences` scaling problem of top-down pipelines and is explicitly intended for high-performance real-time multi-person use.

Reference:
- `open-mmlab/mmpose/projects/rtmo`

Use case in Human-Vision-SDK:
- Preferred for configured `MaxBodies >= 3`.
- Primary target for 4–8-person gameplay.

### 4.2 RTMDet-nano + RTMPose Body26

This is the preferred precision/top-down pipeline for 1–2 people.

The important change from the current project is to use the OpenMMLab real-time combination rather than the existing 640 detector + 133-point whole-body combination:

- RTMDet-nano around 320x320 for person detection.
- RTMPose-t/s Body26 at 256x192.
- Detector runs at a reduced cadence and is also used for reacquisition.
- Between detector refreshes, tracked/pose-derived ROIs are reused.

Reference:
- `open-mmlab/mmpose/projects/rtmpose`

### 4.3 RTMPose Hand21

Hand inference is separated from body inference. The body pipeline supplies wrist/hand ROIs; a small hand model provides real hand landmarks. Hand inference is scheduled independently and does not run for every hand on every body frame.

### 4.4 MediaPipe design patterns to reuse

Human-Vision-SDK should copy MediaPipe's runtime strategy, not its model as the main multi-person model:

- asynchronous/live-stream processing;
- flow limiting and latest-frame behavior;
- detector/reacquisition separated from continuous landmark tracking;
- image/resource pooling;
- Unity rendering decoupled from inference;
- GPU-native input path as a future/optional optimization where practical.

### 4.5 MoveNet MultiPose

Useful performance reference but not selected because the standard multi-pose solution is constrained to fewer people than the SDK's eight-person target and does not provide the required skeleton/hand coverage.

### 4.6 PP-TinyPose

Useful Android performance baseline. Not selected as the core architecture because adopting a second full Paddle deployment stack would increase maintenance cost while OpenMMLab already provides the required body/hand model family.

### 4.7 Ultralytics YOLO Pose

Technically attractive due to deployment breadth, including mobile and embedded exports, but not selected as the core SDK dependency because of commercial licensing implications for a redistributable SDK.

### 4.8 OpenPose / ViTPose / RTMW / DWPose

These are not the Android real-time default. Heavy whole-body models may remain optional desktop/high-accuracy references, but they must not define the Android performance baseline.

## 5. Selected architecture

```text
WebCam / RTSP / Video
        |
        v
Frame Source / Orientation / Analysis Resize
        |
        v
Latest Frame Slot
        |
        v
HumanVision Runtime
        |
        +-------------------- PosePipeline --------------------+
        |                                                      |
        |  PrecisionTopDown                    MultiPerson     |
        |  RTMDet-nano 320                     RTMO-t/s        |
        |       |                                  |            |
        |  RTMPose Body26                          |            |
        |       +------------------+---------------+            |
        |                          v                            |
        |                 HumanVisionTracker                   |
        |          IoU + keypoint + motion + region            |
        |                          |                            |
        |                          v                            |
        |                  stable TrackId                       |
        |                          |                            |
        |                    HandScheduler                      |
        |                   /             \                     |
        |            Left Hand21      Right Hand21              |
        |                   \             /                     |
        |                    SkeletonMapper                     |
        +--------------------------|---------------------------+
                                   v
                           TemporalOutputFilter
                                   |
                                   v
                          60 Hz game-facing state
                                   |
                     C ABI / C# HumanVision API
                                   |
                                   v
                          Unity gameplay + UI
```


## 5.1 Hard isolation requirement: Unity integration must be model-agnostic

This is a release-blocking architectural constraint. Model changes, inference-engine changes and platform acceleration changes must not force changes to game-side Unity integration code.

The project is split into four contracts:

```text
Game / Unity Project
        |
        v
HumanVision.Unity  (stable Unity package)
  - HumanVisionManager
  - HumanVisionCameraManager
  - HumanVisionConfig
  - canonical joint enums and result types
  - region API
  - renderer / HUD
  - stable NativeBindings
        |
        | fixed versioned C ABI
        v
HumanVision Bridge ABI
  - HV_Create / HV_Destroy
  - HV_Configure
  - HV_SubmitFrame
  - HV_GetLatestFrame / HV_CopyBodies
  - HV_SetRegions
  - HV_GetStats / HV_GetCapabilities
        |
        v
HumanVision.NativeRuntime  (replaceable platform runtime)
  - scheduler / latest-frame flow control
  - tracker
  - skeleton mapper
  - temporal filter
  - backend registry
  - pipeline registry
        |
        +--> QNN / DirectML / NNAPI / CPU / future RKNN
        |
        +--> RTMO / RTMPose / future model adapters
        |
        v
HumanVision.ModelPack  (replaceable data package)
  - model files
  - model manifest
  - hashes / licenses
  - input/output metadata
  - decoder/profile identifiers
```

### Non-negotiable dependency direction

Dependencies may only point downward.

- Unity code may depend on the stable C ABI and canonical HumanVision data model.
- Unity code may **not** include `RTMO`, `RTMPose`, `COCO17`, `Halpe26`, `ONNX`, `QNN`, `RKNN`, `ncnn` or provider-specific logic.
- Native runtime may know model/backend details but must convert every model result into the canonical HumanVision result before publication.
- Model packs may be replaced without changing Unity C# code.
- A new backend or a new decoder may require a NativeRuntime update, but must not require changes to the Unity integration package or game scripts.

## 5.2 Package boundary

Do not ship model-specific implementation inside the stable Unity integration package. Use independent artifacts:

```text
Packages/
  com.blazetc.humanvision.unity/        # stable public Unity integration

Runtime/
  windows-x64/                          # replaceable native runtime artifact
  android-arm64/                        # replaceable native runtime/AAR artifact

ModelPacks/
  realtime-multiperson/                 # RTMO profile
  precision-topdown/                    # RTMDet + RTMPose Body profile
  hands/                                # hand model profile
```

The release bundle may contain all artifacts together for convenience, but they remain separately versioned internally. Updating a model pack must not require regenerating Unity scripts, prefabs, scenes, enums or public bindings.

Android note: native accelerator libraries that must be packaged into the APK (for example QNN runtime components) are part of the Android Runtime artifact, not the Unity integration layer. Rebuilding an APK with a newer runtime is expected; rewriting Unity integration code is not.

## 5.3 Stable C ABI contract

The Unity package talks only to a versioned, model-independent C ABI. Existing ABI layouts are never silently changed.

Required rules:

- Every public native struct starts with `struct_size` and a contract/API version where applicable.
- Existing V1 struct field order and size are immutable.
- New fields use a new struct version or new getter entry point.
- Native runtime exposes `HV_GetApiVersion()` and `HV_GetCapabilities()`.
- Unity validates ABI compatibility once at initialization and fails with a clear message if incompatible.
- No model-specific tensor, keypoint index or provider type crosses the ABI.

Canonical ABI results expose only concepts such as body, track id, region index, joint type, normalized/source-space position, confidence, validity, timestamp and age.

## 5.4 Canonical skeleton contract

Unity consumes one canonical joint schema only. All model-native schemas stay below the mapper.

```text
RTMO17 ---------\
Halpe26 ----------> ModelAdapter -> CanonicalSkeleton -> C ABI -> Unity
WholeBody133 -----/
Hand21 -----------/
FutureModel ------/
```

Model adapters are responsible for:

- source keypoint index interpretation;
- coordinate conversion;
- confidence normalization;
- derived spine/clavicle/pelvis joints;
- real hand landmark mapping;
- validity/staleness semantics.

Game code must never branch on which model produced a joint.

## 5.5 Model pack manifest

Models are selected through a data-driven manifest rather than hard-coded model names in Unity. A model pack describes at least:

```text
pack_id
pack_version
pipeline_id
model_role            # detector/body/hand/etc.
model_format
input_contract
output_contract
decoder_id
preferred_backends
fallback_backends
asset_path
sha256
license/source metadata
```

This creates two levels of replaceability:

1. **Same contract / same decoder:** replace or upgrade the model file and manifest only; no NativeRuntime or Unity change.
2. **New model family / new output decoder:** add a NativeRuntime model adapter/decoder; Unity package and game code remain unchanged.

## 5.6 Unity API freeze target

The following game-facing concepts must remain stable across model/backend replacements:

```csharp
HumanVisionManager
HumanVisionCameraManager
HumanVisionConfig
HumanVisionBody
HumanVisionJoint
HumanVisionJointType
GetUsersCount()
GetUserIdByIndex()
IsUserDetected()
TryGetBodyByRegionIndex()
TryGetJointByRegionIndex()
GetJointPosition2D()
GetJointPosition()
GetColorImageTex()
Set/Apply Regions
Stats / capabilities queries
```

`HumanVisionConfig` exposes semantic options such as `MaxBodies`, `PoseMode`, target rates, tracking, hands and backend preference. It must not expose model filenames, tensor shapes or model-specific thresholds as mandatory game integration inputs. Advanced model tuning belongs to runtime/model profile configuration.

A future model replacement is considered architecturally successful only if an existing Unity demo/game scene can switch runtime/model packs and continue compiling without edits to its gameplay scripts.

## 5.7 Compatibility test that Codex must automate

Add a package-isolation compatibility test. It must verify that multiple backend/model fixtures produce results through exactly the same Unity-facing contract. At minimum:

- RTMO fixture -> canonical skeleton;
- RTMPose Body fixture -> canonical skeleton;
- legacy WholeBody fixture -> canonical skeleton;
- hand fixture -> canonical hand extensions;
- all fixtures compile against the same managed assemblies and C ABI headers.

The test must fail if a model-specific type leaks into the public Unity assembly or C ABI.

## 6. Pipeline modes

Introduce a versioned pipeline option:

- `Auto`
- `RealtimeMultiPerson`
- `PrecisionTopDown`
- `LegacyWholeBody` (temporary diagnostic/regression mode only; not Android default)

Default policy:

- `MaxBodies <= 2`: `PrecisionTopDown`.
- `MaxBodies >= 3`: `RealtimeMultiPerson`.

The mode is selected when the SDK starts or settings are applied. Do not oscillate pipelines every frame based on temporarily visible person count because that risks TrackId resets and visible discontinuity.

If the preferred model/backend cannot initialize, the runtime must report the exact fallback path rather than silently pretending hardware acceleration is active.

## 7. Inference backend architecture

Replace the binary "hardware acceleration on/off" concept with explicit backend capabilities.

Game-facing configuration:

- `Auto`
- `QnnHtp`
- `Nnapi`
- `OnnxCpu`
- `DirectML`
- future `Rknn`

Platform preference:

### Android Snapdragon

1. QNN HTP where the model and runtime are supported.
2. NNAPI where appropriate.
3. ONNX Runtime CPU fallback for correctness, with a visible performance warning.

### Windows

1. DirectML.
2. ONNX CPU fallback.

### Future RK3588

1. RKNN.
2. platform fallback.

The runtime must expose the **actual active provider** separately for body, detector and hand models.

## 8. QNN/RTMO deployment risk

RTMO is the algorithmic first choice for 3–8 people, but QNN HTP operator coverage is a deployment risk until validated on hardware.

Codex must therefore implement:

- model/session initialization diagnostics;
- provider capability reporting;
- explicit fallback status;
- no false `HardwareAcceleration=true` status when the effective provider is CPU;
- model/backend isolation so RTMO can be replaced without changing tracking or Unity APIs.

The absence of a physical device during Codex work is not a reason to skip the QNN backend implementation, but it means QNN runtime performance remains a user acceptance item.

## 9. Detector strategy for top-down mode

RTMDet is no longer the global frame-rate controller.

Rules:

- Initial acquisition uses detector.
- Detector refresh defaults to approximately every 5 frames, configurable.
- Stable tracks reuse tracker/pose-derived ROIs between detector runs.
- Detector is triggered immediately when confidence falls, ROI validity fails or reacquisition is needed.
- Detector is allowed to run asynchronously without blocking the game-facing skeleton stream.
- A slow detector result may not overwrite a newer valid track state merely because it completed later.

## 10. Multi-person RTMO strategy

RTMO is run once per selected inference frame and emits multiple body instances.

Postprocessing must:

- enforce `MaxBodies`;
- reject implausible/low-confidence instances;
- map source coordinates consistently through orientation/mirroring;
- feed instances into the common tracker;
- preserve region constraints;
- avoid assigning two detections to the same region slot when region mode is enabled.

RTMO output must not expose model-specific keypoint indices directly to Unity.

## 11. Tracking design

Create/upgrade a model-independent `HumanVisionTracker`.

Association score combines:

- bounding-box IoU;
- keypoint similarity/OKS-like score;
- center-distance gating;
- constant-velocity/Kalman prediction;
- region compatibility;
- track age and confidence.

Use a global assignment step (Hungarian or equivalent) rather than purely greedy nearest-neighbor matching when multiple candidate matches exist.

Track lifecycle:

- `Tentative`
- `Confirmed`
- `Lost`
- `Expired`

Required behavior:

- Crossing people should not trivially exchange IDs.
- Short detector gaps must not delete otherwise valid pose tracks.
- Old delayed detections must not resurrect expired identities.
- Region mode strongly biases/locks a confirmed participant to the configured region slot while valid.

## 12. Hand inference scheduler

Hand inference is optional and asynchronous relative to body inference.

Default scheduling:

- Body target: 25–30 fresh inferences/s when device capacity allows.
- Hand target: 10–15 inferences/s per active relevant hand.
- Raise hand frequency temporarily for fast hand motion or explicit high-detail mode.
- Skip hand inference when hand ROI is too small/low confidence/out of frame.
- Do not run 16 hand models at 30 FPS merely because eight people are visible.

Outputs:

- `Hand`: derived from real hand-landmark geometry/palm region when valid.
- `HandTip`: real index fingertip landmark.
- `Thumb`: real thumb fingertip landmark.

When a real hand result is unavailable or too stale, mark the extended hand joint invalid instead of inventing a fake tracked value.

## 13. Skeleton schema and compatibility

Model-native schemas (`COCO17`, `Halpe26`, `RTMO17`, `Hand21`, `WholeBody133`) stay internal.

Introduce a canonical game-facing skeleton adapter with a Kinect/Azure-Kinect-like body hierarchy. The new canonical public skeleton must contain at least:

- Pelvis / SpineBase
- SpineNavel / SpineMid
- SpineChest / SpineShoulder
- Neck
- Head
- left/right clavicle or compatible shoulder-center mapping
- Shoulder L/R
- Elbow L/R
- Wrist L/R
- Hand L/R
- HandTip L/R
- Thumb L/R
- Hip L/R
- Knee L/R
- Ankle L/R
- Foot L/R

The adapter may derive spine/clavicle center joints from high-confidence body joints. It may not geometrically invent `HandTip` or `Thumb` when real hand inference is required.

### Compatibility rule

Do not break existing 0.3.x consumers unnecessarily.

- Keep legacy 23-joint methods or provide a compatibility facade.
- Add a new versioned 25+/canonical joint API.
- Preserve C ABI compatibility using new entry points or `struct_size` versioning rather than silently changing existing struct memory layouts.

## 14. Temporal output and Unity 60 Hz behavior

Do not claim that 60 Hz game-facing output means 60 neural-network inferences per second.

Maintain separately:

- Camera FPS
- Body inference FPS
- Detector FPS
- Hand inference FPS
- Raw skeleton FPS
- Game-facing output Hz
- Unity render FPS
- result age

The game-facing skeleton stream may update at 60 Hz using a low-latency temporal filter:

- latest valid observation;
- estimated per-joint velocity;
- short prediction horizon, normally around 10–25 ms;
- bounded corrective blending when a fresh observation arrives;
- adaptive/One-Euro-style smoothing where appropriate.

Rules:

- Prediction must be bounded.
- No stale skeleton may be kept alive indefinitely for visual smoothness.
- Track loss or stale age above a configured threshold invalidates the person/joints.
- Raw unfiltered model results remain accessible for diagnostics.

## 15. Unity renderer redesign

Replace the current many-GameObject `Sphere + LineRenderer` debug overlay with a batched UI renderer designed to match the supplied Kinect-style screenshots.

Preferred implementation:

- a custom `MaskableGraphic`/UGUI mesh renderer aligned directly with the camera preview `RectTransform`;
- bone segments rendered as thick quads;
- joints rendered as circular/ring geometry;
- vertex colors support per-person colors;
- invalid bone hidden when either endpoint is invalid;
- optional low-confidence fading;
- no per-joint GameObject required;
- no per-bone LineRenderer required;
- debug labels are optional and disabled by default.

Visual defaults:

- solid bright joint center;
- dark/contrasting outer ring;
- solid bone connection;
- consistent screen-space thickness regardless of camera depth;
- per-track color option;
- Kinect-like topology.

Retain the legacy overlayer only as a diagnostic compatibility component if needed.

## 16. Frame input and preprocessing

0.4 must keep the existing smooth preview path and avoid regressing camera presentation.

Required improvements:

- pool/reuse all analysis buffers;
- no per-frame managed array allocation in hot paths;
- no unnecessary `Texture2D` creation in WebCam hot path;
- preserve latest-frame semantics;
- profile GPU readback separately from inference;
- reduce/replace scalar preprocessing hotspots with optimized implementation where practical;
- support model-specific analysis resolution rather than forcing all models through 640x640.

A full Android zero-copy camera-to-QNN path is desirable but is not required to block 0.4 if the measured primary bottleneck remains model inference. The architecture must leave a clear extension point for GPU/native-buffer input later.

## 17. Segmentation/matting isolation

Segmentation is not part of the body-inference critical path.

When enabled later/currently supported:

```text
Frame Source
  +--> Skeleton Pipeline
  +--> Segmentation Pipeline
```

Skeleton and mask have separate target rates and timestamps. Enabling segmentation must not synchronously stall the skeleton pipeline.

## 18. Profiler and diagnostics

Expand statistics so Android performance can be diagnosed from a user screenshot without source-level guessing.

Required fields include:

- camera/presentation FPS;
- analysis submit FPS;
- dropped/replaced frame count;
- GPU readback/copy time where measurable;
- detector preprocess / inference / postprocess / total;
- body preprocess / inference / postprocess / total;
- hand preprocess / inference / postprocess / total;
- tracking time;
- body count;
- raw result age;
- game-facing result age;
- selected pose pipeline;
- requested backend;
- actual detector/body/hand provider;
- provider fallback reason;
- model names and input sizes.

No per-frame logcat spam. HUD uses rolling values/averages.

## 19. Configuration surface

The Unity-facing configuration should remain simple. Proposed high-level options:

```csharp
new HumanVisionConfig
{
    MaxBodies = 6,
    PoseMode = HumanVisionPoseMode.Auto,
    Backend = HumanVisionBackend.Auto,
    TargetBodyFps = 30,
    TargetSkeletonOutputHz = 60,
    EnableHands = true,
    HandTargetFps = 15,
    EnableTracking = true,
};
```

Advanced detector intervals, thresholds and provider controls remain available for debugging but should not be required for normal game integration.

## 20. Model/package policy

The stable Unity integration package must contain no hard-coded dependency on a specific body/hand model. Native runtimes and model packs are independently replaceable artifacts.

0.4 release model packs must identify every model by:

- upstream project/model name;
- source/download reference;
- license;
- input/output contract;
- hash;
- intended pipeline;
- target platforms/backends.

Do not silently overwrite old model files under the same name/hash contract.

Candidate set:

- RTMO-t 416 and/or RTMO-s 640 for multi-person.
- RTMDet-nano 320 for top-down acquisition.
- RTMPose-t/s Body26 256x192.
- RTMPose Hand21.
- legacy 133 model retained only if needed for regression/desktop diagnostic mode.

The final packaged set should avoid unnecessary duplicate large models.

## 21. Platform strategy

### Windows

- Keep DirectML support.
- Run RTMO and top-down pipelines through the common model interfaces.
- CPU fallback remains available.

### Android Snapdragon

- Add QNN/HTP support as the preferred accelerated backend.
- Keep explicit NNAPI and CPU fallback modes.
- Do not silently report QNN when a graph fell back entirely to CPU.

### RK3588 future path

- Keep the inference backend abstraction clean enough to add RKNN without changing body/tracker/skeleton/Unity APIs.
- Model conversion artifacts belong to the RKNN backend/package layer, not gameplay code.

## 22. Build and verification contract for Codex

Codex must verify what can be verified without the user's hardware:

- native Windows compile;
- native Android ARM64 cross-compile;
- managed Unity Runtime/Demo/Editor compile;
- ABI/static layout tests;
- model decoder golden tests where reference outputs are available;
- tracker unit tests including crossing/reacquisition cases;
- skeleton mapper tests;
- temporal filter tests;
- package generation;
- package manifest/hash consistency;
- no duplicate Unity GUIDs;
- release archive inspection.

Codex must **not** run or claim:

- OnePlus runtime FPS acceptance;
- camera quality acceptance;
- sustained thermal acceptance;
- real 8-person acceptance;
- physical RTSP/camera field validation;

unless the user explicitly supplies results or later authorizes such tests.

## 23. User manual acceptance matrix

The release HUD must make the following tests easy for the user to perform:

| Scenario | What user checks |
|---|---|
| Camera only | preview is smooth and orientation correct |
| 1 person | low latency, stable skeleton, stable ID |
| 2 people | independent skeletons, no ID swap |
| 4 people | stable multi-person tracking and acceptable body FPS |
| 6 people | RTMO multi-person throughput and region stability |
| 8 people | functional maximum-person behavior and device performance |
| Crossing | IDs do not trivially swap |
| Leave/return | old skeleton disappears; reacquisition works |
| Stationary | skeleton does not flicker/disappear |
| Fast motion | low result age; no long stale trails |
| Hands | real HandTip/Thumb appear when sufficiently visible |
| Rotation | orientation change resets coordinates safely |
| Long run | temperature/performance degradation is observed |

Performance is evaluated using both FPS and latency/result age. A visually smooth but 300 ms stale skeleton is not acceptable.

## 24. Success criteria for 0.4 architecture

Architectural success requires:

1. Android is no longer hard-wired to the current 640 FP32 detector + 133-point serial whole-body pipeline.
2. Multi-person RTMO pipeline exists behind a common interface.
3. Precision RTMDet-nano + RTMPose Body26 pipeline exists behind the same interface.
4. Real hand landmarks are handled by a separate scheduler/model.
5. Tracking is model-independent and preserves region semantics.
6. Game-facing canonical skeleton is model-independent.
7. The Unity integration package and public C# API contain no model/backend-specific code.
8. Model packs can be changed without modifying Unity gameplay scripts or public managed assemblies.
9. New model families are isolated behind NativeRuntime adapters and the stable C ABI.
10. Unity rendering is decoupled and batched.
11. Backend/provider selection and fallback are observable.
12. Codex can compile/package/publish without requiring a phone.
13. User can manually measure all runtime acceptance metrics from the shipped demo/HUD.

Performance targets such as exact 30 FPS at 8 people are **manual hardware acceptance targets**, not claims made by build completion.

## 25. Out of scope for the first 0.4 implementation

Unless required by implementation feasibility, do not expand scope into:

- metric depth/true 3D reconstruction from a monocular RGB camera;
- face mesh/expressions;
- gesture recognition framework beyond exposing skeleton data;
- cloud inference;
- network multiplayer;
- automatic game-specific running/jumping classifiers;
- full Android Camera2 rewrite solely for zero-copy before inference bottlenecks are addressed.

These can be separate milestones after 0.4 runtime validation.

## 26. Implementation-order constraint

The detailed Codex implementation plan must follow dependency order, approximately:

1. freeze the Unity public API and versioned C ABI;
2. split stable Unity integration, replaceable NativeRuntime and ModelPack artifacts;
3. add model-pack manifest + adapter/decoder registry;
4. model-independent pipeline abstraction;
5. canonical skeleton/mapper and tracker contracts;
6. RTMDet-nano + Body26 reference path;
7. RTMO decoder/pipeline;
8. hand model/scheduler;
9. Android QNN backend and fallback reporting;
10. temporal output filter;
11. Unity batched Kinect-style renderer;
12. package-isolation compatibility tests;
13. package/build/release docs and GitHub publication.

Exact task boundaries are defined in the implementation plan after this design is approved.
