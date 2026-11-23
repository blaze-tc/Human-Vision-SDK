# Independent input native plugin

## Independent live GPU preview (candidate 2026-11-23)

`include/humanvision_input_live.h` adds a versioned function table without
changing any existing Input or NativeV1 structure/function. Attach a live
consumer to an existing `OpenNativeV1` handle; this never opens another decoder
or claims an analysis publication. The same acquired PRIVATE AImage is sampled
only on the native producer device, under one acquire/return proof. Analysis and live output have independent
three-slot RGBA AHB pools and latest-only publication. A full/disabled live pool
does not stop analysis; held analysis outputs do not stop live-only conversion.
The preview bounds/transform are independent of model dimensions. Exact CPU
capture remains a separate explicit request with the existing contract.

The new Unity bridge imports completed **RGBA live AHBs**, never the PRIVATE
decoder image. It copies them on Unity's actual Vulkan device/serialized queue
into three borrowed Unity RenderTextures. It never wraps a foreign-device
VkImage in `CreateExternalTexture`, maps pixels to CPU, or uses the 5 Hz exact
capture path for live display. Native AHB and Unity target leases are separate;
the native live lease returns only after the actual Unity copy fence. A target
held by a Unity sampler/copy remains leased until the caller's real GPU fence
and `ReleaseSlot(slot, target_generation, sequence)`. Unobserved images may be
replaced, so a slow/resumed consumer gets the newest available image rather
than replaying a queue. There can still be one real copy in flight to retire.

The producer live shader writes bottom-left rows; analysis/exact keep their
original top-left rows. Live payloads contain encoded display RGB, not linear
RGB. Use `R8G8B8A8_SRGB` targets in a Linear Unity project and
`R8G8B8A8_UNorm` in a Gamma project. The bridge uses same-size RGBA32
`vkCmdCopyImage` to preserve texel codes, without a blit/color conversion or
double encoding. Normal sRGB texture sampling provides the Linear-project
decode; a Demo-specific material must not be required. Source color metadata
is not invented: explicit IEC61966-2-1 transfer is sRGB, other transfers remain
unknown. True GPU orientation/default-sampling color checks remain device
acceptance, not a host test result.

Each `PrepareRenderEvent` reservation must be followed by
`GL.IssuePluginEventAndData(GetRenderEventFunc(), HV_INPUT_LIVE_RENDER_EVENT_ID,
consumer)`. The consumer retains the source and queued callback lifetime.
Retire targets, release held slots after sampling fences, and continue issuing
retirement events until `TargetsRetired` succeeds. `Close` is nonblocking;
`TryRelease` stays BUSY until callbacks, copies, imported memory and target
leases actually retire. Input `Release` stays BUSY while a consumer exists.
Serialize final consumer `TryRelease` against every other consumer API call;
OK invalidates that handle. Unproven GPU completion quarantines resources.

`GetSourceProgressV1` reads real Android native-source cumulative codec-output,
acquired-image, live publication, analysis publication/claim and live-drop
counts without a model-result/claim gate. Source geometry, state and timestamp
come from the source/explicit Input monotonic clock. Other source paths return
unsupported/INVALID rather than reporting fabricated zero activity. These are
distinct stages; native publication counts are not Unity display FPS.

## Autonomous Android Vulkan source

The optional source ABI in `include/humanvision_input_native.h` takes the
platform-selected hardware AVC codec, RTSP options, output dimensions, rotation,
mirror and readback mode. Call `HV_Input_OpenNativeV1` from a control thread.
Open checks the actual Vulkan 1.1 device and UUIDs, sampler YCbCr conversion,
foreign ownership, external RGBA memory and bidirectional sync-fd support.
Each live native session owns its codec, Vulkan instance, device, queue,
worker, import cache and three analysis slots. Use `HV_Input_GetNativeDeviceV1`
and `HV_Input_GetNativePoolV1` at startup to match the consumer device and warm
persistent imports. The three RGBA AHB addresses stay valid until source
release. The codec name and decoder capability selection belong to the caller.

The producer wakes on decoded images and slot returns. It retains the newest
pending decoder image while consumers hold all slots. It converts PRIVATE YCbCr
with the same sampler and crop shader as the Unity adapter. Output rows start at
the top. Every publication includes its decoder generation and sequence, color
declarations, source crop, transformed dimensions, PTS availability and local
decoder clock. The compressed video access unit carries its original media PTS,
validity and local receive timestamp across decoder reordering. Unmatched packet
timing has `pts_valid=0` and `received_timestamp_us=-1`. A genuine zero PTS
is valid. Each dequeued picture carries the format snapshot for its own Surface
release timestamp. The reader discards delayed pictures from an older contract,
including changes to crop origin or color at the same visible size.
A readback-enabled source copies the identical conversion target
to persistent host-visible staging. Its fence and noncoherent invalidate must
both succeed before publication.

- `HV_Input_WaitNativeFrameV1` waits on a condition variable on the consumer
  worker. `HV_Input_AcquireNativeFrameV1` checks immediately. Either call
  acquires a publication exactly once and transfers the exported sync fd.
- For GPU use, import or close that fd exactly once. Acquire the AHB image from
  EXTERNAL_KHR in GENERAL layout, return ownership to EXTERNAL_KHR, and observe
  the actual consumer fence before calling `HV_Input_ReleaseNativeFrameV1`.
  A completed producer proof can be represented by fd -1. For CPU reads,
  `HV_Input_CopyNativeRgbaV1` checks the leased slot, generation and sequence.
- `HV_Input_RequestNativeCaptureV1` requests one full visible-crop RGBA capture
  with the next admitted analysis frame. Its extra dispatch and staging copy
  use that same decoded image and submission. `HV_Input_CopyNativeCaptureV1`
  with a null destination returns the required bytes and exact frame metadata
  once ready. A successful copy consumes the capture. Repeated requests are
  bounded to one outstanding capture. During the caller's full-resolution
  memcpy, analysis can keep advancing.
- `HV_Input_Close` stops admission and wakes waiters. Outstanding completed
  frames can still be released. `HV_Input_Release` returns BUSY until the
  decoder, producer and all leases retire. A failed post-submit fence with
  unproven completion quarantines the owned hardware resources through process
  exit. The failure string remains available from `HV_Input_GetLastError`.

The existing Windows source exposes top-left CPU frames through
`HV_Input_Open`, `HV_Input_PollFrame` and `HV_Input_CopyRgba`.
Windows reports an explicit platform error for the Android Vulkan open.

## Model-sized Android output and RTSP probing

An Android Vulkan source can bind an optional three-texture `512×288` analysis
output before binding its preview textures. Both outputs use the decoder frame's
sequence, generation and timestamps. With both slots free, two dispatches
read the same decoded image within one queue submission. They write separate
Unity textures through separate descriptor sets. Both slots publish after the
shared fence signals. With no free preview slot, analysis can submit on its
own. Its decoded image can then supply one fallback preview if a slot opens
and no new analysis is ready. A new paired submission replaces the fallback.
The scheduler caps preview dispatch age at 120 ms from local decode. The
acknowledgement records that an observed analysis frame was offered to the
consumer. Preview-only clients keep using the existing binding and poll API.

The native `input_analysis_cadence` record includes batch starts, replaced
fallbacks, completed analysis output, acknowledgements and preview discard
reasons. `input_gpu_cadence` reports preview publications and callbacks where
the conversion fence was still pending in the same interval. A shared fence
includes both dispatches in each branch's submit-to-observation time. When a
decoder changes geometry on the same connection, a new generation clears the
fallback and pauses both output rings until managed textures match the new crop.

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

Production on HLK-AL00 / Android 10 / `OMX.hisi.video.decoder.avc` limits the
PRIVATE reader to three acquired images. The decoder accepted 13 output buffers
after reconfiguration in the device probe and rejected 14 or more. MediaCodec
still writes to the same type of PRIVATE GPU surface; only the reader capacity
changes. Other devices and diagnostic gates keep the four-image reader.
`hardware_codec`, `decoder_first_keyframe_submitted`, `decoder_first_output`,
`decoder_first_image` and `decoder_output_error` in the `HVInputGate` log locate
startup failures without changing reconnect behavior. See the [device report](../../docs/reports/2026-11-18-hlk-al00-rtsp-first-frame.md).

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
