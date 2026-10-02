# Independent input native plugin

Consumes compressed H.264 over RTSP TCP. Produces Windows CPU RGBA, or Android
PRIVATE decoded AHB leases for GPU conversion. It has no SDK, Runtime Host,
model, ORT, ncnn or SDK clock dependency. The V1 SDK ABI is separate and stable.

`src/android/mediacodec_source.cpp` selects the actual hardware AVC codec and
queries decoder crop/color metadata; `ahb_capabilities.cpp` qualifies the actual
AHB and successfully created Unity logical device. `unity_input_vulkan.cpp`
accesses Unity textures through AccessTexture and submits only within serialized
AccessQueue callbacks. `ahb_image_cache.cpp` caches by AHB identity, generation
and import contract, retiring removed imports only after real GPU completion.
YCbCr conversion uses queried AHB components/offsets and reliable decoder or H.264
SPS/VUI matrix/range; vendor metadata retains its own domain. The bounded SPS
parser reads compressed syntax only. Unknown transfer/primaries remain unknown.
Startup waits boundedly for an actual keyframe before submitting compressed data
to MediaCodec. Imported images are sampled
read-only. No decoded plane mapping, CPU conversion or production readback exists.

Android production RTSP now publishes three fixed Unity-owned RGBA output slots.
`RtspFrameSource` owns native worker, generation and texture lifetimes on the main
thread. It initializes each Unity target with Unity rendering before native bind.
There is one serialized converter command/fence, not three parallel conversions.
The AHB import cache is independent of output slots. Pending decoded frames are
latest-only; no-free-slot drops are counted. The compressed application backlog
retains one packet; decoder pressure or transport failure recreates the reader,
decoder and RTSP session and waits for a real keyframe. Healthy admitted P/B
packets are preserved. Missing reliable crop/color/capabilities fail explicitly.

Task3 V1 layouts/exports remain unchanged. Android `HV_Input_PollFrame` only reads
completed metadata and does not retain an output. The additive Android
`HV_Input_PollGpuFrame` returns and atomically acknowledges the exact latest
sequence/generation/slot; `HV_Input_ReleaseGpuSlot` returns that observed slot only
after consumer source-copy fences finish. Superseded never-observed publications
are retired as drops, while observed slots cannot be overwritten until released.
`BindGpuTargets` binds exactly three borrowed Unity textures; `GetGpuGeometry`
reports actual pending crop geometry, not request dimensions. All GPU publication
requires actual converter fence completion. Positive acquire fd import consumes a
duplicate only on success, preserving the original AImage-owned fd on failure;
FOREIGN queue ownership return, release-fd export and AImage_deleteAsync remain.

Close requests cancellation without a main-thread join. A persistent managed
retirement pump continues render-event polling after disable/destroy. Native
worker release requires real queued GPU retirement; texture destruction also
requires all actual consumer copy fences. App pause closes and resume waits for
retirement before reopening. Opening/Reconnecting/Error/Closing status includes
an actionable reason; initialization failures retain Error on following ticks.
No source Close waits for inference or uses queue/device idle. Production never
uses the private color/copy probe APIs or decoded CPU planes/readbacks.

The Lifecycle fixture is input-only and owns a temporary RTSP publisher/ADB
reverse mapping. Its private diagnostic consumer uses a real Vulkan image copy
and fence because the tested Unity2021 target cannot poll its async-compute
GraphicsFence. It retires its own reusable command pool/fence after completion.
The runner saves actual source/native/APK hashes, an action trace and a live
production preview screenshot, then removes its test stream. Reopening that APK
alone does not establish a reusable standalone demo; final samples are Task10.
Acquire fds on the prior target were all -1; physical positive-fd qualification
remains separate from meaningful host ownership tests. This input lifecycle gate
does not certify skeleton FPS, sensor timestamps or the 25/30 FPS recognition goal.

Build/test: `pwsh -File tools/package/build_input_native.ps1 -Platform Android
-ApiLevel 26 -RunTests`. Host cache/color tests execute Windows PE targets under
`out/input-native/windows`. The shader builder requires the pinned NDK23.1.7779620
compiler hash. Device proof: `tools/test/collect_android_input_gate.ps1 -Gate Lifecycle` and `-Gate Color`;
matrix/range/crop options identify encoded source fixtures and actual pixel checks.
Missing formats/features reject explicitly. A request or entry point is not proof
that Unity enabled a capability; actual device capture and GPU gate establish it.
