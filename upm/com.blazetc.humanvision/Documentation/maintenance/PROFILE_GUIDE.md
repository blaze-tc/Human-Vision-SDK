# Profiles

`profiles/auto.json` selects a body pipeline by configured capacity (1–2 TopDown,
3–8 RTMO), an independent hand pipeline and available accelerated/CPU backends.
`cpu.json` is the same composition with CPU-only execution for comparison.
Selection uses configured maximum people, not transient detections.

Windows realtime and Android realtime start with auto. Android precision can copy
auto to a new profile ID and use a single body selection for pipeline.topdown and
precision-t-26; pose cost then scales with people. This is a configurable accuracy
tradeoff, not a measured throughput claim. A future RK3588 preset must reference a
registered, tested backend and compatible pack; no RKNN implementation is shipped.

Schema=1; `profile` must match filename. Choose either body or body_by_capacity
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
native tests. Hash validation and incompatible capacity fail initialization.
