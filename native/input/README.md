# Independent input native plugin

## Model-sized Android output and RTSP probing

An Android Vulkan source can bind an optional three-texture `512×288` analysis
output before binding its preview textures. Both outputs use the decoder frame's
sequence, generation and timestamps. The analysis conversion runs first and
publishes after its GPU fence completes. The converter can then retain that
decoded image as one pending preview with an independent fence and slot lease.
A ready analysis frame takes priority over pending preview. When analysis
cannot run, a pending preview can use a free preview slot during the first
120 ms after analysis completion. The acknowledgement records that an observed
analysis frame was offered to the consumer. Preview-only clients keep using
the existing binding and poll API.
The native `input_analysis_cadence` record reports completed analysis output,
acknowledgements, decoded-frame replacement and preview pressure once per
interval. When a decoder changes geometry on the same connection, a new
generation pauses both output rings until managed textures match the new crop.

The internal `HV_Input_ProbeRtsp` binding on Android and Windows checks H.264
dimensions over RTSP TCP. It inspects codec parameters or SPS data and uses a
single deadline for connection and compressed reads. The binding opens no
decoder. Callers probe away from the Unity main thread.

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
The AHB import cache is independent of output slots. It admits a fixed domain of
64 AHB object identities following the API26-era AOSP BufferQueue policy. This
is not an allocation-ID API or a guarantee that every vendor/future decoder has
64 buffers; a 65th retained object fails explicitly with its full cache snapshot.
An existing object reuses its import until callback removal or contract/generation
replacement; no LRU or per-frame import recreation is used. A fixed known-import
registry reserves the source-reader/object pair before import, coalesces repeated
removals and counts unknown callbacks without consuming positions. Callback only
compares raw identity values; it never dereferences a reader or destroys Vulkan
resources. The decoder owns/deletes its reader, whose listener/looper is quiesced
before that reader domain can be reused; callback/decoded/cache AHB references
retain object identity through their respective lifetimes. Removed busy imports
keep their references and resources until the real GPU fence completes.

Listener registration fails explicitly if unavailable. A once-at-stop diagnostic
reports cache imports/resources and extra AHB reference balance after worker and
GPU retirement. Decoder image/lease/fd counters remain a separate ownership
domain. Cache/Vulkan errors preserve their first stage and result through decoder
interruption and reconnect; Vulkan result values are not formatted as FFmpeg errno.

Pending decoded frames are latest-only. Conversion can use a free output slot
while Unity has an unobserved publication. If all slots hold live work or leases,
conversion waits for a free slot. The compressed application backlog
retains one packet; decoder pressure or transport failure recreates the reader,
decoder and RTSP session and waits for a real keyframe. Healthy admitted P/B
packets are preserved. Missing reliable crop/color/capabilities fail explicitly.

Task3 V1 layouts/exports remain unchanged. Android `HV_Input_PollFrame` only reads
completed metadata and does not retain an output. The additive Android
`HV_Input_PollGpuFrame` returns and atomically acknowledges the exact latest
sequence/generation/slot; `HV_Input_ReleaseGpuSlot` returns that observed slot only
after consumer source-copy fences finish. Conversion preserves an unread
publication until a newer fence completes. MediaCodec releases buffers in
decoder output order at least 24 ms apart while ImageReader continues polling.
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

## Nonblocking Android publication polling (candidate2026-10-09)

The GPU metadata getter can check an already-signaled conversion fence before
observing its exact output slot, instead of waiting for the next render callback
and another managed update. Pending/error fences keep the AImage lease and never
publish a new frame. No GPU wait/queue submission, Unity API call or Vulkan object
destruction occurs in this completion-only helper; render-event Poll still drains
removed imports and retires resources. The session mutex serializes both paths.
The three-slot observation/copy-lease contract and C exports are unchanged.
This is a pacing/latency candidate; see the OnePlus report for actual acceptance.

## Independent codec output (2026-10-12 candidate)

The compressed RTSP reader no longer owns the output-drain schedule. A scoped
output worker drains MediaCodec and acquires latest AImages while TCP input may
be waiting for another packet. Its first output dequeue waits at most 2 ms,
remaining dequeues are nonblocking, and each pass has a 1 ms cancellable idle
wait. Codec/reader destruction and reconnect always join this worker first.
No additional frame queue, CPU pixel mapping, or inference instance is added.

`input_decode_cadence` records received video packets, codec inputs/outputs, and
acquired images once per second. Packet rate is explicitly not camera FPS.
The codec interval matches queue-begin and output-dequeue by PTS. Ambiguous,
missing or skipped timestamps are counted separately.
`input_gpu_cadence` records render callbacks, busy/empty callbacks, conversion
completions, ring drops and pending-image replacements. Ring drops include both
slot pressure and superseded unobserved publications. Decode-to-submit and
submit-to-observed-fence intervals include host scheduling/queue time; they are
not GPU execution timestamps. Compare these with managed unique publication
counts to locate loss instead of inferring camera speed from inference FPS.
These logs are captured by the existing Settings Demo native log collector.

Host regression `input.OutputPump.IndependentDrainAndRetirement` covers output
arrival without a second input packet, bounded idle polling, repeated teardown,
and exception forwarding. OnePlus testing does not certify RK3588 throughput.

The managed RTSP owner also issues one render event after `WaitForEndOfFrame`.
This provides a second opportunity to observe completion/start the latest image
when the Update event was too early. It does not publish frames, block on a
fence, or create another converter/inference instance. Pause/close/disable stop
this opportunity; the existing Tick path alone publishes completed frames.
The targeted lifecycle test verifies no duplicate publication and cancellation.
When returning a replaced pending decoder image, never cancel the output slot
owned by a distinct active image awaiting Unity's queue callback.
