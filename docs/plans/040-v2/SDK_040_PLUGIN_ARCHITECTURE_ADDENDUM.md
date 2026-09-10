# Human-Vision-SDK 0.4 Plugin Architecture Addendum

This document supersedes any conflicting architecture details in the earlier 0.4 plan.

## Core rule

Unity-facing code is a long-lived stable product surface. Recognition algorithms, model files, tensor layouts, inference providers, tracking implementations and device accelerators must remain replaceable below a stable Host ABI.

```text
Unity Game / Scene / Prefab
        |
HumanVision Unity SDK  <-- stable
        |
Stable C ABI / Host ABI  <-- stable
================================================
        |
HumanVision Runtime Host
        |
+-------+----------------------+------------------+
|                              |                  |
Pipeline Plugins          Backend Plugins    Common Services
|                              |                  |
RTMO                         ORT              Tracking
RTMPose TopDown              QNN              Skeleton Mapping
Legacy WholeBody             DirectML         Temporal
Future Pose X                RKNN             Region
                             ncnn             Diagnostics
        |
Model Packs
        |
Profiles
```

## Plugin deployment model

Use a hybrid plugin architecture:

- Windows may load pipeline/backend plugins as DLLs.
- Android 0.4 may compile/register the same plugins statically inside the native library.
- Both paths use the same versioned C-compatible Plugin ABI.
- Do not expose C++ STL/classes across DLL/SO boundaries.

Required ABI concepts:

```text
HV_QueryPlugin
HV_PluginApiV1
HV_PipelineApiV1
HV_BackendApiV1

struct_size
api_version
plugin_id
plugin_version
capabilities
```

## Responsibilities

### Runtime Host

Owns lifecycle, profile loading, plugin registry, ModelPack registry, capability matching, frame scheduling, diagnostics and public C ABI coordination. It must not contain RTMO/RTMPose/QNN-specific branches.

### Pipeline Plugin

Owns algorithm-specific preprocessing, model invocation orchestration and decoding. It outputs generic BodyObservation/HandObservation only. It must not own Unity types, global TrackId, regions or drawing.

### Backend Plugin

Owns session creation, tensor binding and inference execution for one execution technology. It must not decode pose semantics.

### ModelPack

Contains model assets + manifest only. Compatible model replacement should need only a new ModelPack directory and profile change.

Example:

```text
modelpacks/
  rtmo-t-body7-416/
    manifest.json
    model.onnx
```

### Profile

Selects the composition without changing source code:

```json
{
  "profile": "android_realtime",
  "body": {
    "pipeline": "rtmo",
    "modelPack": "rtmo-t-body7-416"
  },
  "hands": {
    "enabled": true,
    "pipeline": "rtmpose_hand",
    "modelPack": "rtmpose-hand21",
    "fps": 15
  },
  "backend": {
    "preference": "auto"
  },
  "tracking": {
    "provider": "humanvision_tracker"
  },
  "output": {
    "skeleton": "humanvision_v1",
    "hz": 60
  }
}
```

## Capability-driven selection

Do not use model-name conditionals in Host/common services. Plugins declare capabilities such as:

```text
body_pose
hand_pose
multi_person
max_people
segmentation
dynamic_input
batch
gpu_input
```

Profiles declare requirements. Runtime resolves compatible components and reports a useful incompatibility reason.

## Canonical skeleton boundary

All body/hand schemas map into one internal canonical skeleton before Unity. Unity never knows COCO17, Halpe26, RTMO17 or WholeBody133 indices.

## Maintenance goal

A future change should normally map to exactly one area:

| Change | Primary area |
|---|---|
| Replace compatible weights | `modelpacks/<pack>/` |
| Change model resolution/normalization supported by same plugin | ModelPack manifest |
| Add a new pose algorithm | `runtime/plugins/pipeline/<id>/` |
| Add a new accelerator/inference framework | `runtime/plugins/backend/<id>/` |
| Change identity association | `runtime/services/tracking/` |
| Change joint mapping | `runtime/services/skeleton/` |
| Change smoothing/prediction | `runtime/services/temporal/` |
| Change regions | `runtime/services/region/` |
| Change Unity drawing only | UPM renderer |
| Change runtime composition | `profiles/*.json` |
| Change gameplay calls | only when intentionally changing stable Unity API |

## Documentation is part of the architecture

0.4 is not complete until the maintenance documentation and automatic documentation guards described in the v2 maintenance plan are implemented.
