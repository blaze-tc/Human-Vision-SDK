# ARCHITECTURE.md

# HumanVisionSDK Architecture - Demo First

## 1. Layering

```text
Unity Demo
  |
  | HumanVision C# API
  v
NativeBindings (P/Invoke)
  |
  v
C ABI: humanvision_c.h
  |
  v
HumanVisionEngine
  |-- LatestFrameSlot
  |-- InferenceWorker
  |-- ResultSnapshotStore
  |
  +--> DetectorModel (RTMDet)
  +--> IBodyTracker
  +--> PoseModel (RTMPose)
  +--> StatsCollector
  |
  v
IInferenceBackend
  |
  +--> OnnxRuntimeBackend (D0)
  +--> future RKNN/TensorRT/etc.
```

Video sources are outside the model pipeline:

```text
Unity VideoPlayer -> RawFrame submit ---+
Unity WebCamTexture (later) ------------+--> HumanVisionEngine
Native RTSP source (D1) ----------------+
```

## 2. Threading

Unity must never wait for inference.

```text
Unity Main Thread
    |
    | HV_SubmitFrame(copy/reuse, fast return)
    v
LatestFrameSlot  <--- overwrite unprocessed old frame
    |
InferenceWorker
    | detector -> tracker -> pose
    v
Back Result Snapshot
    |
atomic/synchronized publish
    v
Front Result Snapshot
    |
Unity polls/copies bodies
```

If frames arrive at 30 fps and inference runs at 15 fps, the system processes recent frames and increments `dropped_frames`; it must not create a 1-second queue of stale frames.

## 3. Model boundaries

### Detector

Input: normalized detector tensor.  
Output: source-space person candidates `{bbox, score}`.

### Tracker

Input: person detections + timestamp.  
Output: detections with `track_id` and track state.

### Pose

Input: source frame + tracked person ROI.  
Output: COCO-17 source-space joints + confidence.

### Unity adapter

Game-specific transformations such as left-to-right participant ordering, region assignment, or mapping to a Kinect/MediaPipe-like schema belong above the HumanVision body result.

## 4. Memory rules

- Preallocate frame buffers after first resolution/configuration.
- Reuse detector tensor buffers where runtime API permits.
- Reuse body result vectors with capacity >= `MaxBodies`.
- Managed Unity body arrays/lists are reused.
- No LINQ/ToArray in `Update()`.
- Avoid Texture creation/destruction on every frame.

D0 may perform one frame copy at the C ABI boundary to establish clear ownership. Zero-copy/GPU input is an optimization stage, not a correctness requirement.

## 5. Error/state model

HumanVision manager exposes a small state machine:

```text
Uninitialized -> LoadingModels -> Ready -> Running
                                  |          |
                                  v          v
                                Error <----- RecoverableInputError
```

RTSP adds:

```text
Disconnected -> Connecting -> Streaming -> Reconnecting
```

Model-load errors are fatal for the current configuration. RTSP disconnect is recoverable.

## 6. D0 repository target

```text
HumanVisionSDK/
  AGENTS.md
  README_FIRST.md
  docs/
  native/
    include/humanvision/
      humanvision_c.h
      humanvision_types.h
    src/
      core/
      backend/onnx/
      models/rtmdet/
      models/rtmpose/
      tracking/
    CMakeLists.txt
  tests/
    native/
    golden/
    testdata/
  tools/
    reference/
    benchmark/
  models/
    detector/
    pose/
  unity/
    HumanVisionDemo/
      Assets/
        HumanVision/
          Runtime/
          Demo/
        Plugins/x86_64/
```

Do not create Android/RKNN directories during D0 unless a build system requires an empty cross-platform placeholder; prefer not to.
