# Profiles

`profiles/auto.json` selects a body pipeline by configured capacity (1–2 TopDown,
3–8 RTMO), an independent hand pipeline and available accelerated/CPU backends.
`cpu.json` is the same composition with CPU-only execution for comparison.
Selection uses configured maximum people, not transient detections.

Android provider comparison uses `android-cpu-nohands`,
`android-nnapi-nohands`, and `android-xnnpack-nohands`. These diagnostic profiles
keep the same body selection and rates as `auto`, disable the independent hand
pipeline, and each request one backend only. Failure to create that backend makes
the run fail; it must not be counted as a successful measurement of the requested
provider. They do not replace `auto` as the default profile.

Production Android builds use exactly one of `android-ncnn-vulkan`,
`android-ort-xnnpack`, or `android-ort-cpu`. These profiles name one body pipeline,
one ModelPack, and one explicit backend. They set `allow_fallback` to `false` and
declare the capabilities needed by the complete composition. A requirement is
satisfied by the selected pipeline, ModelPack, or backend; initialization reports
the first missing capability by name.

```json
{
  "schema_version": 1,
  "profile": "android-ncnn-vulkan",
  "body": {
    "pipeline": "pipeline.topdown",
    "modelPack": "precision-t-26-ncnn-fp16"
  },
  "hands": {"enabled": false, "fps": 15},
  "body_fps": 30,
  "output": {"hz": 60},
  "required_capabilities": [
    "body_pose",
    "multi_person",
    "gpu_input",
    "vulkan",
    "fp16-storage",
    "fp16-arithmetic",
    "android-hardware-buffer",
    "external-sync-fd"
  ],
  "backend": {
    "preference": ["backend.ncnn.vulkan"],
    "allow_fallback": false
  },
  "staged_dependencies": {
    "modelPacks": ["precision-t-26-ncnn-fp16"],
    "backends": ["backend.ncnn.vulkan"]
  }
}
```

The NCNN profile is a staged Milestone A/B contract. Normal architecture checks
accept only the explicitly listed missing dependencies. Runtime/build validation
continues to fail, and `check_architecture_boundaries.py --release` or `--package`
rejects every staged profile. Remove each staged entry only after the real component
or verified ModelPack exists. The two ORT production profiles use
`pipeline.topdown`, `precision-t-26`, and exactly one of `backend.ort.xnnpack` or
`backend.ort.cpu`; their requirements are `body_pose`, `multi_person`, and
`tensor_inference`.

Windows realtime and Android realtime start with auto. Android precision can copy
auto to a new profile ID and use a single body selection for pipeline.topdown and
precision-t-26; pose cost then scales with people. This is a configurable accuracy
tradeoff, not a measured throughput claim. A future RK3588 preset must reference a
registered, tested backend and compatible pack; no RKNN implementation is shipped.

Profile schema=1; `profile` must match filename. Choose either body or body_by_capacity
(strictly increasing max_people up to8). Each choice names pipeline and modelPack.
Hands has enabled/pipeline/modelPack/fps. Backend preference is an ordered array;
auto expands available tensor backends by priority. Unsupported candidates retain
fallback reasons. body_fps and hands.fps are submission ceilings, not guarantees.
Hand fps limits each hand independently. One asynchronous worker accepts at most
two ROIs per job and rotates fairly across eligible hands, prioritizing motion
within each round. Actual cadence is bounded by camera and inference capacity;
it is not guaranteed for every hand of eight people.
output.hz describes the desired sampling cadence; Unity samples on rendered frames
using the same monotonic clock. Sampling never increments raw inference FPS.

Validate profile references with the architecture guard and Profile/RuntimeSession
native tests. Hash validation, missing named requirements, and incompatible capacity
fail initialization. Production profiles must never use `auto` or more than one
backend while fallback is disabled.
