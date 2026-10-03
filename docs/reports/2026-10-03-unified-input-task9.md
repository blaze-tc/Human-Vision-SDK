# Task9: unified SDK input adapter and exact GPU copy tickets (2026-10-03)

Fresh task spec and quality review pass. Stable public skeleton APIs now accept
independent unified frame sources. Three fixed normalization targets and source
leases retire against source/generation/frame/reservation-bound native VkFences;
source preview continues independently when recognition is detached or rejected.
V1 signatures/layout remain stable. Android NCNN fails clearly rather than
switching backends or reading full images to CPU. Explicit old CPU routes remain
compatible. Actual canonical and UPM Android routes are qualified separately.

The original gates passed native92/92, canonical6/6, UPM6/6, both Android
IL2CPP/Vulkan builds and production API26 guard-OFF export audit. Fresh review
then found four regressions; the scoped fixes and affected builds are qualified
separately below. Original passing APKs do not qualify changed native source.
Meaningful raw failures and repairs remain. Exact source and artifact maps
were verified independently before delivery.
Existing unrelated R4 work and caches remain unstaged and preserved.

Snapdragon888 controlled RTSP recognition shows seven people with visible bones
in a captured sample; continuous tickets and detach/preview lifecycle pass with
terminal resources and errors zero. This is functional input acceptance, not
final skeleton performance acceptance. Fresh complete observation FPS7.858103,
age P50/P95206.107/311.822ms, GPU capture15.72886FPS and output sampling60.0844FPS
are distinct metrics. The25FPS source cannot certify30 fresh complete frames/s.
GUIRepaint intervals are not physical presentation timing. Failed screenrecord
attempts provide no video proof; observed stutter is not declared solved.

The private green diagnostic overlay intentionally omits prior head/points/colors;
public joint mapping is correct. Task10 must reuse the existing tested overlay,
not the diagnostic drawing. Task10 three Demos/settings/clean input-only and
combined package imports and Task11 acceptance/maintenance remain outstanding.
No main merge, Release, model/backend or Renderer performance change is admitted.

## Frozen implementation, commands, artifacts and qualifications

# Task9 implementer report

Status: DONE_WITH_CONCERNS. Implementation and declared behavioral/build/recognition gates passed and source/artifact maps frozen. Preview cadence, startup gap, unavailable video recording, and existing profile performance limits remain explicit. No staging or commit performed. Base is Task8 `69071cdcb52f58428d98315f19d5b0032cf15c64`.

## Behavior and ownership

The optional unified input adapter binds/detaches independent sources, preserves their preview, submits supported actual texture frames through existing selected SDK routes, and preserves old live/video/camera public methods. Android hardware RTSP requires the explicitly selected GPU route; unsupported geometry/backend combinations retain preview with actionable errors. Windows/Editor existing CPU submission remains compatible. UPM receives production routes from clean HEAD plus Task9 changes, not retained R4 diagnostics. Both packages explicitly reference Input; semver dependency alone does not install a Git Input package. Task10 still needs actual two-package import verification.

Task8's generation retirement token remains Close/detach-only. Task9 adds a 64-byte source/frame/generation/event/reservation-bound ticket, prepare/poll/cancel APIs, and exact real VkFence acknowledgment. Outstanding tickets prevent EventRecord reuse until consumption. A fully unissued reservation can acknowledge dropped/unsubmitted without GPU work. If Unity normalization was already queued, native normalization-only retirement submits a reused real fence after `AccessQueue(flush=true)` on the exact Unity VkQueue; it cannot publish AHB/inference or count as copied. Failed/unknown completion quarantines strong owners. Tests distinguish this case from actual AHB copy and inference lifetime. V1 signatures/layout and fixed-pointer semantics remain unchanged; V2 admits three actual target identities under one same contract/generation.

Three adapter normalization targets, fences, and source-copy leases are reused only after the exact old lease slot/identity was consumed (`SourceCopyLease.IsRetired`). Native source views are cached by actual VkImage/contract across AHB slots, bounded to three identities per slot, retired with generation/contract changes. The one explicit top-row adapter blit follows source's already upright/mirrored preview. Existing native shader bit2 selects exact texelFetch, **not Y flip**; shader was not modified. Prior Task8 semantic-label error remains in historical evidence and is corrected by root's public documentation.

Real synchronization evidence combines pinned Unity header flush contract, Vulkan same-queue submission fence scope, focused host lifecycle tests, and physical exact queue/fence/command handles. Host Vulkan simulation is not hardware proof. Native output/source/inference lifetime tests explicitly reject inference-complete or timer/aggregate-counter substitutes for source-copy completion.

## Meaningful RED/GREEN and actual builds

- Initial missing ticket API/behavior RED: `out/input/task9-native-red-build.log`, `task9-native-red-behavior.log`. Working focused GREEN60/60: `task9-native-green3.log`.
- Actual input lease reuse RED/GREEN: `out/input/task9-input-red`, `task9-input-green`; old copied leases, repeated Poll, new slot/generation, and incomplete retirement are covered.
- Actual canonical adapter RED4 failures/1 old-API pass then GREEN5/5: `task9-managed-red-canonical`, `task9-managed-green1-canonical`. Clean candidate canonical and UPM GREEN5/5 receipts retained separately.
- Actual UPM CS0122 compiler failure retained in `task9-managed-green2-upm/unity-retry.log`; sole approved Demo friend addition fixed it. UPM GREEN5/5 `task9-managed-green3-upm`.
- Production multi-image worker guard was missing after Prepare supported V2. Meaningful host control-path RED0 copies/60 frames: `task9-native-adapter-red.log`; correction GREEN focused case: `task9-native-adapter-green2.log`. The earlier stale Ninja no-work attempt and incomplete fake AccessTexture/Queue fixture failure remain failures, not credited passes.
- Fresh clean candidate7 native focused CTest92/92 passed: `task9-candidate-host-round7.log`. Native production unchanged since that run.
- Physical APK7 found actual capture provenance ABI rejection (2 SensorVerified is unsupported). Named enum forwarding behavioral RED expected1/actual2: `task9-provenance-red-canonical/tests.xml`. Correction forwards UnityObserved=1 while retaining real decode/PTS clock metadata; actual canonical6/6 and UPM6/6 GREEN in `task9-provenance-green-{canonical,upm}/receipt.json`.
- Actual canonical candidate9 Android IL2CPP/Vulkan compile and APK succeeded sequentially: `task9-device-round6/unity-build-retry.log` (actual Unity return0), `build-receipt.json`. APK SHA256 `5a9a13f13761d2214252b3f6552dbb9b9bd25fe964d49043fcd4e8e3c129a0b3`.
- Actual UPM candidate9 Android IL2CPP/Vulkan compile and APK succeeded: `task9-upm-android-round2/build-receipt.json` and `unity-build.log`.
- Production API26 `HV_ANDROID_GPU_GATE=OFF` native build and export audit passed: `task9-production-api26-*`. Three admitted ticket exports present, private HV_Task/GpuGate exports absent. Qualified NDK23.1.7779620, Unity2021.3.45f1, v14314.44 host qualification retained. First audit used incorrect expected V2-suffixed names and failed; exact public header names then audited successfully.

## Physical controlled real-person gate

Device `e7c07019`, owned MediaMTX/FFmpeg publisher and USB reverse, independent RTSP source, public adapter, actual SDK models, MaxBodies8. Source geometry640x360/25FPS is separate from unchanged model contracts detector320x320, pose192x256, cadence4. Pack `out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16`; profile `profiles/android-ncnn-vulkan.json` SHA256 `20e2714759f03fee79a94d0dcf7ccc80700785a2cb191a4a01b403e0b975d0b2`. Exact pack/model/native/source/APK hashes are in actual build receipts.

Authorized original video1 SHA256 `e3620101d8218e7e9f2736cf5dab7a497bfcfc23e33a40244b63ae317c1bb0c8`, original37..49s, scaled640x360 with32-pixel asymmetric corner markers, H26425FPS. Derived fixture SHA256 `ca6815fa6f05973f3842cf8fd9d4430dff9ff7eb510e5cd0cff6f777d3f7607d`, ffprobe/encode/sample evidence under `out/input/task9-fixture`. Earlier Task4 fixture was0..8s, not the dance37 segment; caught and corrected before Task9 actual recognition replay.

First actual APK7 run failed for65s with copied0, bodies0, visible bones0 and explicit capture-provenance rejection. Human reported stutter and no skeleton. `task9-device-round4/failure-preview.png` was captured after cleanup and depicts Android launcher, not gate/video/error; logs and human observation establish that failure. Do not use that PNG as live preview proof.

Corrected APK9 first replay was aborted by collector's old `mResumedActivity` label assumption; actual Android `topResumedActivity=` verified the exact gate package. Raw failure/foreground/cleanup archived in `task9-device-round6/replay-attempt1`. Collector now accepts exact current/old resumed labels and checks them before screenshots.

Corrected APK9 second replay passed: `task9-device-round6/logcat.txt`, actual `recognition.png` and `detached-preview.png` with foreground receipts. Images were opened and inspected. Recognition image contains upright real dancers, asymmetric markers, and green skeletal edges on seven people **in that sample**; no claim of complete continuous tracking of all7. Gate accumulated92 nonempty fresh result observations and7699 valid bone edges. Actual295 unique reservations acknowledged real fences across slots0/1/2 (144/77/74), one queue handle and three fixed reused fence/command handles. No outcome2/drop-only acknowledgments occurred in this physical replay; dropped/normalization-only paths have meaningful host tests rather than invented physical proof. Independent preview advanced647->677 after detach with pending retirement0. Terminal SDK snapshot `0,0,0,0,0,1,295,0`; actual Input753 submissions/completions, all owners/cache/view/AHB/FD resources zero, GPU errors0, CPU image readbacks0. Cleanup verified owned processes terminated/exited and USB reverse removed.

## Preview stutter investigation and evidence limits

The fixed8192 diagnostic arrays record only Unity GUIRepaint new-frame draws (frame ID/PTS/texture/time/phase). They are **not physical swapchain presentation**. No perframe logging occurs during measurement; cold samples dump after terminal. SurfaceFlinger layer list is captured; Android gfxinfo contains view hierarchy but no qualifying Vulkan presentation frame timings. Bounded screenrecord launch produced no remote file (empty stdout/stderr, after-cleanup pull failed); it provides no video proof or successful recording interval assertion.

`task9-device-round6/presentation-analysis.json`: SDK-uninitialized baseline21.62 new draws/s, p95gap67.90ms/max70.61ms; attached24.11, p9569.10ms/max99.75ms; detached24.36, p9567.97ms/max70.81ms. Transition into initialization includes225.64ms gap. No PTS regression across12s loops, but PTS skips max160ms attached and80ms baseline/detached. Input decodedDrops31/slotDrops2. Steady pending leases0; no three-lease deadlock or inference-wait stall demonstrated. Mobile gate repaint is approximately30/s versus25FPS source and produces uneven33/67ms draw cadence even without SDK. Recognition and copy-lifetime criteria passed; these measurements do not certify smooth display,30 fresh skeleton FPS, complete Hand/Handtip/Thumb acceptance, production camera acceptance, or final demo acceptance. Residual cadence and startup gap are not dismissed by25FPS publication counters.

## Preservation and final freeze

Current before snapshots use the actual post-Task8 working bytes, not an earlier baseline. Retained original R4 plugin128/-11 and canonical bridge+9 plus caches remain unstaged. Clean-candidate mapper overlays Task9-only edits onto clean HEAD with strict protected-overlap rejection; only approved clean `_source.HasRecentFrame` equivalent adaptation replaces working retained R4 SourceIsCurrent guard. Native ncnn pin uses exact LF->CRLF byte equality plus qualified provenance receipt hash equality, never weakened guards.

Latest production/APK source is immutable `out/input/task9-candidate-round9`. Current host collector is newer; final maps must distinguish it and prove unchanged production APK material rather than relabel APK source. Ignored freeze helper prepared, not yet run. Final flat before/working/candidate/protected/artifact maps, sourcepatch, source ZIP and normalization proof paths will be recorded here after the final decision. Root owns status/public maintenance reports/plan/index/commit and fresh spec+quality review; Task10 is not authorized by this report.

## Final bounded60 replay and schema clarification

Root approved exactly one private fixture60-target experiment, plus a cold bounded reflection snapshot of existing RuntimeStatsV2. Production source, native binary, models, detector cadence and Renderer were unchanged. Target60 build round7 finished before the stats edit but was never installed/run; abandoned APK preserved. Actual revised fixture APK8 `task9-device-round8/task9-adapter.apk` SHA256 `cb68cb51be8bcc80e741dcd8e95147e08cd54e0993077a7f6fe0f02c2e18e60b`, actual Unityexit0, reused native SHA256 `180b2cdf54adadddbb91038defa321f12da8ce700e3f67e75031b1d959ebd060` verified, all production source bytes equal candidate9. Actual APK8 source root `out/input/task9-candidate-final2`; frozen final3 differs only in host collector, not APK production/gate bytes. UPM actual Android source compile remains independently proven by APK9 because production bytes are identical; no inference/native suites were rerun for a private fixture-rate/diagnostic change.

ONE actual60-target replay passed:739 preview frames,115 fresh nonempty gate observations,9632 valid bone edges;298 real copy acknowledgments, terminalSDK `0,0,0,0,0,1,298,0`. Actual Input741 submissions/completions, all owners/views/AHB/FD/cache returned zero, GPUerrors0/readbacks0, decodedDrops4/slotDrops1. Steady pending0/1 reflects normal transient leases; detach retires and independent preview continues. Fresh recognition and detached PNGs were opened and inspected and foreground exact package was verified. NativeStats snapshot: FreshObservationFrames129, FreshObservationFps7.858103, OutputSamples998, OutputSamplingFps60.0844, GpuCaptureFps15.72886, SourceFrameId556, CaptureTimestampUs2576649001940/PublicationTimestampUs2576649178892, AgeP50/P95206.107/311.822ms, PoseAge206.806/272.435ms, SensorCaptureAgeNaN (unavailable), CaptureProvenance1 (UnityObserved), noFreeSlotDrops240/supersededReadyDrops2, copy/import/poseValidationErrors0. Actual counter/time observations are separate from SDK windowed FPS; no person multiplication, rendered samples do not become fresh results, zero/partial frames are not forced success. Initial live counters showed bodies0/sequence0 while backend warmed; later captured sample showed7. Full Hand/Handtip/Thumb and 30FPS acceptance are not established by this profile/run.

Frame duplication handling: source SourceGeneration contract requires monotonically increasing FrameId/publication time across generations; Android producer increments frameId without reset. Adapter queries strictly after last handled FrameId and advances it on accepted, busy or error outcomes, so 60 render ticks do not resubmit an unchanged25FPS source frame. Result counting requires a changed raw ResultSequence, nonempty bodies and current adapter result admissibility; sampled/predicted outputs are not counted. Physical APK8 logs have298 ticket entries,298 unique `(source_id,generation,frame_id)` tuples and298 unique reservations. Exact source/generation/reservation binds copy acknowledgments; source switch/generation retirement invalidates prior result range and minimum sequence. Source publication provenance is UnityObserved in InputMonotonic clock while real decode timestamp/clock ID and stream PTS retain their original metadata.

60-target Unity GUIRepaint metrics (not swapchain): baseline24.60 draws/s, P50/P95/max34.21/51.07/70.33ms; attached24.90,34.72/52.14/70.02; detached24.52,41.32/51.72/68.31. Crossphase initialization168.60ms, versus prior225.64ms. P95 improves from approximately69 to52ms, indicating30/25 draw alias contributes, but residual timing/decoder drops remain and physical smoothness is not certified. Direct bounded device nohup screenrecord PID5023 attempted10s with start receipt; remote process exited, device log empty, no MP4, pull failure preserved. Recording activity interval cannot be positively qualified, so do not use it as video proof or assert measured smoothness. No further device experiment/tuning performed.

Human noted diagnostic bones differ from prior renderer. Verified stable public17-joint compatibility array maps through existing RuntimeSession LegacyMap `{27,28,30,29,31,5,12,6,13,7,14,18,22,19,23,20,24}`: gate5/6 shoulders,7/8 elbows,9/10 wrists,11/12 hips,13/14 knees,15/16 ankles match canonical named IDs. Gate intentionally uses minimal green3px limb/direct torso rectangle lines and omits head/face, derived spine/clavicle anchors, rings, colors and widths. Existing HumanVisionOverlay uses Coco17Skeleton.Bones/TryResolveAnchor in Demo/OverlayGeometry.cs with those additional anchors/style. This is different diagnostic topology/style, not wrong model indices/schema. Task10 must reuse existing tested renderer rather than private gate drawing. No production Renderer changed.

## Immutable handoff

Frozen clean candidate: `out/input/task9-candidate-final3`.
Flat maps: `out/input/task9-freeze/{before-map,working-map,candidate-map,protected-map,protected-hunks,artifact-map}.json`.
Source patch: `out/input/task9-freeze/task9-source.patch` SHA256 `9dd300f2d942e0905102c9e530f5a34d5b9ce2ac3f2243eeec791103354d62fd`; read-only `git apply --no-index --check --reverse --directory=out/input/task9-candidate-final3` PASS, receipt `out/input/task9-freeze-patch-check.log`.
Source ZIP: `out/input/task9-freeze/task9-frozen-source.zip` SHA256 `56dfb6985539726d369df1fe45a34be3403c71db9c41fe3768609126a2e8fec3`.
Freeze receipt60 owned working/candidate files,237 unchanged protected files,665 actual artifact hashes. All artifact-map hashes rechecked against current bytes after freeze, no drift. Explicit strong normalization proof `out/input/task9-freeze/ncnn-normalization-proof.json` retains exact LF->CRLF bytes and qualified ncnn receipt hash, no weaker guard.

Original plugin128/-11 and canonicalbridge+9 retained added hunks all present exactly and excluded from clean candidate. Protected-hunks map has only two expanded camera return-guard lines marked not identical: original `!IsReady || !SourceIsCurrent` becomes that retained condition plus new adapter `!CanPresentResult`, both canonical/UPM. This is admitted Task9 stale-result guard augmentation, with old hook retained working/excluded clean by approved `_source.HasRecentFrame` equivalent.237 other before files unchanged; no unrelated dirty R4/caches removed.

Two sourcepatch-generation failure attempts are preserved `out/input/task9-freeze-attempt1` (Windows patch text newline conversion) and `task9-freeze-attempt2` (new-file CRLF data normalized accidentally); final patch preserves actual raw newline bytes and passes check. Third intermediate freeze had valid patch but stale pre-check artifact hash and is retained in `task9-freeze-attempt3`; final freeze reread immutable PASS log before hashing. No source/API/model change occurred during these artifact fixes. Working/candidate distinct hashes and exact before snapshots remain available. Parent owns all public docs/staging/commit; source/code and artifact writers stopped before handoff. Root fresh spec and quality reviews required before Task10.


## Initial task review: four required fixes

Spec compliance: FAIL.
Code quality: FAIL — Needs fixes.

### Spec Compliance

- The declared adapter, compatibility wrappers, native V2 ticket APIs, actual UPM GPU route/selector, assembly references, and package dependency are present. The approved additive native scope is appropriate; V1 C layouts/signatures remain unchanged in the reviewed diff (`native/include/humanvision/humanvision_android_gpu.h:84`, `runtime/composition/android_gpu_c.cpp:97`).
- Important findings I1–I4 below prevent accepting stable old API/Windows CPU compatibility and correct normalization-only retirement.
- Scope remains Task9: no model/profile/renderer changes in this diff. Task10's clean two-package import and final three Demos remain later acceptance; the private gate's green diagnostic bones do not establish final renderer parity (`tools/test/Task9AdapterGate.cs:16`, `tools/test/Task9AdapterGateBuild.cs:11`).
- Cannot verify all physical modes or smooth display from this diff. Existing recorded evidence supports controlled RTSP recognition/retirement; it does not establish camera acceptance, Hand/Handtip/Thumb completeness, a successful recording, or the 30 fresh complete skeleton FPS target. These limits alone are not Task9 functional failures.

### Strengths

- Exact source/frame/generation/event/reservation tickets prevent event-record reuse until acknowledgment; `PollCopy` checks the real slot submission fence and consumes the ticket while inference may remain leased (`runtime/gpu/android/unity_vulkan_bridge.cpp:357`, `:391`; `tests/runtime/test_unity_vulkan_bridge_contract.cpp:312`). Cancel-unissued and queued normalization-only retirement have different paths (`runtime/gpu/android/unity_vulkan_bridge.cpp:377`, `:432`, `:663`).
- Three preallocated adapter slots retain source and normalized textures; reuse additionally waits for the exact old input lease identity to be consumed, addressing reusable-fence aliasing (`unity/HumanVisionDemo/Assets/HumanVision/Demo/Input/HumanVisionInputAdapter.cs:18`, `:123`; `upm/com.blazetc.humanvision.input/Runtime/SourceRetirement.cs:30`, `:179`; `upm/com.blazetc.humanvision.input/Tests/EditMode/FrameContractTests.cs:19`). Generation retirement is used at detach/switch, not once per frame (`HumanVisionInputAdapter.cs:62`, `:130`).
- Android NCNN routes through GPU reservations before normalization, never the CPU submission branch; Android hardware RTSP with a CPU runtime fails clearly while the source remains independent (`HumanVisionInputAdapter.cs:68`; `upm/com.blazetc.humanvision/Runtime/HumanVisionRuntimeSession.cs:84`). UPM gets its own production GPU bridge and conditional selector (`upm/com.blazetc.humanvision/Runtime/Android/HumanVisionAndroidRuntimeSelection.cs:13`).
- The preview remains source-owned; adapter detach does not close it. Row normalization is explicit once, and named UnityObserved provenance uses the actual ABI value 1 (`HumanVisionInputAdapter.cs:46`, `:104`; `HumanVisionAndroidGpuFrameBridge.cs:198`; `InputAdapterTests.cs:27`). Shader bit2 is not a Y flip; the shader is outside this task diff and unchanged according to the controller's protected-file verification.

### Issues

#### Critical

- None identified.

#### Important

- **I1 — Preserve the actual UPM two-argument public signature.** `upm/com.blazetc.humanvision/Runtime/Demo/VideoPlayerFrameSource.cs:340` replaces the baseline `SubmitExternalTexture(Texture,long)` with only a four-argument method whose last two arguments are optional. Source calls can still compile, but the old CLR method identity disappears: reflection using the old parameter list and consumers compiled against it cannot resolve it. The new UPM reflection test checks only the canonical four-argument signature (`upm/com.blazetc.humanvision/Tests/EditMode/InputAdapterTests.cs:98`), missing the packaged baseline. Add an explicit two-argument forwarding overload and retain the new routes; test both actual UPM old two-argument and canonical old four-argument signatures. Evidence: the diff itself contains the removed two-argument declaration and replacement, so no additional code crawl or test rerun was necessary.

- **I2 — Normalization-only retirement falsely initializes the AHB destination layout.** `runtime/gpu/android/unity_vulkan_plugin.cpp:1572` records an empty command for source retirement, and `runtime/gpu/android/unity_vulkan_bridge.cpp:663` submits it through the ordinary `Submit` dispatch. That shared submission sets `destination_initialized = true` (`unity_vulkan_plugin.cpp:1594`), although this command never transitions or accesses the AHB destination. A new destination was created with `VK_IMAGE_LAYOUT_UNDEFINED`; the next real copy's `Begin` only overrides its old layout to UNDEFINED when this flag is false. Thus a first-slot warm-up/drop can make the next real copy use an EXTERNAL/GENERAL old layout and ownership state that was never established. Preserve destination state for source-only commands, setting it only after a command that actually performs the destination transitions. Add a production adapter regression that does normalization-only retirement before a slot's first real copy and checks the subsequent destination barrier's old layout and ownership. Focused named-risk check: the diff cuts `Begin` off before its barriers, so I read candidate `unity_vulkan_plugin.cpp:1462–1491` and searched only `destination_initialized`/`initialLayout` in that same file; `:1247` and `:1482` confirm the initial and conditional old-layout states. Existing fake completion tests and successful physical replay do not cover this actual layout defect; the physical replay reported no outcome2 acknowledgments.

- **I3 — First CPU submission erases existing preview scene bindings.** Both adapter copies call `cpuBridge.Configure(manager,null,null)` even when `GetComponent<VideoPlayerFrameSource>()` returns the already configured legacy scene bridge (`unity/HumanVisionDemo/Assets/HumanVision/Demo/Input/HumanVisionInputAdapter.cs:88`; UPM same line). `Configure` assigns those nulls to `targetDisplay` and `aspectRatioFitter`, so the new `LateUpdate`/`PresentLiveTexture` forwarding can no longer update the legacy RawImage or its layout. This affects Windows/Editor video/camera and explicit Android CPU modes, which this task must preserve. Configure a newly created bridge as needed, and bind the manager on a reused bridge without clearing its display/fitter references. Add a Windows CPU adapter regression with a preconfigured RawImage/fitter, then submit and advance frames and verify that the same bindings continue receiving the independent preview. Focused named-risk check: read only candidate canonical `VideoPlayerFrameSource.cs:491–501` and corresponding UPM lines; both directly assign `targetDisplay = display` and `aspectRatioFitter = fitter` at `:498–499`. The reviewed `PresentLiveTexture` context guards updates on these references at `:286–287`.

- **I4 — Actual preview geometry overwrites CPU analysis-buffer geometry.** New `LateUpdate` writes `SourceWidth/SourceHeight` from `_unifiedSource.CurrentTexture` (`unity/HumanVisionDemo/Assets/HumanVision/Demo/VideoPlayerFrameSource.cs:219`; UPM same line), while the existing CPU branch allocates a bounded analysis texture/buffer from `AnalysisRenderTextureGeometry.CalculateTargetSize` (`:380`). For a 1920x1080 source using the default 1280x720 analysis cap, LateUpdate changes these fields to 1920x1080 before the async completion callback. That callback uses the fields to normalize rows and submit width/height/stride for the smaller buffer; subsequent submissions also treat the same allocation as a geometry mismatch and repeatedly tear down/reallocate it. Keep actual preview dimensions distinct from the existing CPU readback geometry, and bind callback geometry to the accepted slot/analysis allocation. Test a larger source through the actual CPU adapter, including a delayed readback callback, verifying correct accepted width/height/stride, bounded buffer size, and no repeated allocation after warm-up. Focused named-risk check: the changed submission hunk is cut off before completion, so I read candidate `VideoPlayerFrameSource.cs:672–718`, plus the focused `SourceWidth/SourceHeight` assignments. `:698–699` and `:706–712` confirm callback dependence; `:727–728` set analysis geometry. The same reviewed UPM migrated CPU machinery uses these fields. Existing adapter tests exercise uninitialized/no-lease preview and do not enter this CPU branch (`InputAdapterTests.cs:15`, `:81`).

#### Minor

- The new public ticket comment says DROPPED_UNSUBMITTED means no GPU command sampled the source (`native/include/humanvision/humanvision_android_gpu.h:85`). Approved normalization-only retirement can include the already queued Unity source normalization; the outcome means no SDK AHB copy/inference publication, not no GPU source read. Clarify the comment alongside I2 so callers understand why outcome2 may require a real fence.
- Existing host warnings remain visible: C4996/C4100/C4456 and pinned Unity header encoding warnings (`out/input/task9-candidate-host-round7.log:1941`, `:3656`, `:5269`, `:14439`). These are calibrated legacy/toolchain noise and do not justify widening this task.
- The test named `UnsupportedPortraitPreviewContinuesButInferenceFails` checks the geometry helper, and `SlowInferenceNeverThrottlesPreview` tests an uninitialized SDK (`InputAdapterTests.cs:60`, `:81`). Their assertions are meaningful but narrower than their names; the physical RTSP gate complements GPU behavior, while the CPU integration gaps are actionable under I3/I4 rather than a request for indiscriminate broader coverage.

### Checks and evidence limits

- Read the scoped clean-candidate diff in sequential chunks; expanded only cut-off functions and concrete named-risk checks listed under I2–I4. No git commands, builds, suites, subagents, source edits, index edits, or commits were performed. Only this ledger review was written.
- Controller's fresh verification establishes the 60 working/60 clean candidate sources, 237 protected files, 665 artifact hashes and ZIP; those hashes were not re-run in this review. Frozen reviewed candidate is `out/input/task9-candidate-final3`, base `69071cdcb52f58428d98315f19d5b0032cf15c64`.
- Read retained evidence rather than regenerating it: `out/input/task9-candidate-host-round7.log:15182` reports 92/92; `out/input/task9-provenance-green-upm/receipt.json` reports actual source-specific 6/6; `out/input/task9-upm-android-round2/build-receipt.json` identifies the separately compiled packaged source. Production APK qualification must retain the controller's exact source-map binding; the newer collector is not the APK source.
- `out/input/task9-device-round8/logcat.txt:6973` records FreshObservationFps 7.858103 and AgeP95 311.822ms; `:8200` records detached preview advancement 636→666 with retirement pending0; `:9078` records terminal SDK resource zeros with 298 copies; `:9083` records 739 preview frames,115 nonempty fresh gate observations,9632 bone edges. These support the controlled physical recognition/ownership gate, while 30FPS remains unmet. GUIRepaint timing is explicitly not physical swapchain timing (`out/input/task9-device-round8/presentation-analysis.json:2`); failed screenrecord provides no video proof. One visible seven-person sample is not proof of stable all-person tracking or smoothness.

### Assessment

Task quality: Needs fixes. Spec compliance FAIL and code quality FAIL until I1–I4 receive minimal fixes and focused red/green evidence. The core exact-ticket and independent-preview design is sound, but legacy CPU compatibility and the first normalization-only native retirement case contain concrete regressions not answered by the current passing gates.


## Scoped fix round1 evidence

# Task9 fix round1 implementer handoff

Status: DONE_WITH_CONCERNS. I1–I4 and Minor M1 have minimal fixes with fresh scoped RED/GREEN evidence. Root's fresh specification/quality review is still required; this report does not approve Task10 or skeleton performance acceptance.

Base remains `69071cdcb52f58428d98315f19d5b0032cf15c64`. Worktree: `E:/Project/Human Vision SDK/.worktrees/android-ncnn-vulkan`. No staging/commit, device run, model/profile change, production renderer change, new CPU fallback or new public test hook. Round0 report, final3, freezes, raw failures, APKs and caches remain unchanged.

## Fixes and behavioral evidence

I1: The UPM `VideoPlayerFrameSource` now retains explicit public `SubmitExternalTexture(Texture,long)` and forwards it to the four-argument route. Canonical's original four-argument method remains unchanged. UPM reflection checks both CLR identities. Actual UPM RED2 failed with the old two-argument reflection result null; fresh UPM GREEN passes.

I2: A slot records whether its completed command actually contains destination transitions. Successful submission only marks the AHB destination initialized for a real recorded Blit/Color copy; source-only retirement records clear that command state while preserving any previously initialized destination state. The production Android adapter regression records/submits source-only retirement before the slot's first copy, observes no barriers, rejects completion while its real simulated fence is not ready, and verifies the next first destination barrier is UNDEFINED with zero source access and queue families IGNORED. After a real copy and another source-only retirement, the next copy still reacquires EXTERNAL/GENERAL ownership into graphics family5. Existing release to EXTERNAL and semaphore export remain intact. Minor M1 now states dropped means no SDK AHB copy/inference publication; prior Unity normalization may require a real fence.

This first-use exception is restricted to the SDK's newly allocated, never-written destination image (`AHardwareBuffer_allocate`, exclusive `VkImage` created with initialLayout UNDEFINED). It does not apply to imported camera/native-written sources or a destination previously released to NCNN. Vulkan's [resource sharing contract](https://docs.vulkan.org/spec/latest/chapters/resources.html#resources-sharing) makes a new exclusive resource initially unowned and implicitly acquired by first use; [queue transfers](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-queue-transfers) allow discarding unneeded contents. The [AHB memory contract](https://docs.vulkan.org/spec/latest/chapters/memory.html) distinguishes EXTERNAL matching Vulkan driver ownership from FOREIGN native APIs. These focused authoritative references were supplied/checked by root for the exact first-use ruling. Subsequent transfers retain the established EXTERNAL protocol.

I3: Both adapters bind the manager on an existing CPU bridge without replacing its serialized RawImage/fitter. A newly added bridge still gets full Configure. The actual Editor CPU test preconfigures display/fitter, acquires a real input source lease, executes production adapter→Graphics.Blit→AsyncGPUReadback→manager.SubmitFrame and advances independent preview. It verifies the original bindings, preview texture and aspect ratio survive.

I4: CPU allocation reuse compares the actual analysis RenderTexture dimensions. Accepted readback slots retain Width/Height belonging to their own allocation; callback row normalization and submitted width/height/stride use those captured dimensions. Unified preview SourceWidth/SourceHeight reflect the actual preview separately. The HD test queues 1920×1080, calls LateUpdate before the async callback completes, verifies actual preview geometry, then records submission at 1280×720, stride5120, bytes3686400. A second accepted frame reuses the same RenderTexture allocation and completes another submission. No new Android NCNN/RTSP readback route was introduced.

The CPU session spy implements the existing internal IHumanVisionSession solely to record the real managed submission boundary. It supplies no fake model bodies/results and makes no native inference/performance claim. Tests set row calibration ready to isolate these regressions; calibration itself is outside this fix. EditMode explicitly initializes the real bridge's Awake pool because MonoBehaviour's normal Play Mode lifecycle is absent.

## Actual commands and results

All commands ran from this worktree. Every run used a new immutable output directory.

- `tools/test/task9_managed_verify.ps1 -Kind canonical -Output out/input/task9-fix1-red2`: expected RED6/8, I3 null preview binding and I4 submitted width1920 instead of1280.
- Same with `-Kind upm`: expected RED5/8, those CPU regressions plus I1 missing two-argument CLR method.
- `tools/test/task9_managed_verify.ps1 -Kind canonical|upm -Output out/input/task9-fix1-green`: actual working source GREEN8/8 each, Unity2021.3.45f1 D3D11 Editor CPU conditionals.
- Same with `-SourceRoot out/input/task9-fix1-candidate -Output out/input/task9-fix1-candidate-green`: actual clean candidate canonical8/8 and UPM8/8, exit0, failed0. `out/input/task9-fix1-build-receipt.json` hashes actual staged .cs/.asmdef compilation inputs and binds candidate-owned source bytes.
- `cmd /c L/task9-round1/host.cmd out/input/task9-fix1-native-red-source out/input/task9-fix1-native-red`: expected RED31/32. I2 observed GENERAL(1) instead of UNDEFINED(0), EXTERNAL→family5 instead of local first ownership. Raw `out/input/task9-fix1-native-red.log` preserved.
- Same with source `out/input/task9-fix1-candidate`, build `out/input/task9-fix1-native-green`: GREEN32/32 production Android adapter tests, v14314.44. Host uses simulated Vulkan callbacks executing production recording/submission code, not physical GPU proof. Other original native92/92 evidence is retained as round0 and was not rerun unchanged.
- CMake configured the clean candidate into `out/input/task9-fix1-api26` with qualified NDK23.1.7779620, arm64-v8a, android-26, Release, BUILD_TESTING=OFF, HV_ANDROID_GPU_GATE=OFF and unchanged pinned NCNN/ORT roots; `cmake --build ... --target humanvision --parallel 6` exited0. Configure/build logs retained. `verify_android_gpu_bridge_gate_libs.ps1 -Mode Stage` into a new isolated closure directory passed AArch64/API26 dependency closure; llvm-readelf confirms the three admitted ticket exports and no private task/gate exports. Audit/symbol raw logs retained.

Fresh gateOFF binary: `out/input/task9-fix1-api26/bin/Release/libhumanvision.so`, SHA256 `fef5fda3952d21e68b40052cc8804adcd99bc9d961429daf868afd3650bbbd96`. Build receipt records143 native source/header/CMake inputs, complete owned source hashes, unchanged locked NCNN provenance/qualified receipt hashes, and actual staged managed source maps.

Initial CPU RED attempts (`task9-fix1-red-{canonical,upm}`) exposed missing EditMode Awake setup before the intended callback assertions. They are retained as harness failures, not claimed as I3/I4 behavioral RED. Corrected RED2 provides the actual regression evidence. A transient Python shell quoting failure was corrected with a here-string; no source was modified by that failed command. UPM production changes were remapped narrowly from its own before bytes; canonical R4 evaluation hunks are excluded from UPM and clean candidates.

## Frozen inputs for root

`L` is `.superpowers/sdd/2026-10-01-unity-unified-input-and-standalone-rtsp`.

- Final candidate: `out/input/task9-fix1-candidate`; exact path in `L/task9-round1/candidate-path.txt`.
- Full60 owned paths: `working-map.json`, `candidate-map.json`. Only10 changed paths: `fix-delta-map.json`. Exact before working bytes: `before/` and `before-map.json`.
- Scoped fix diff relative to original clean final3: `review.diff`.
- `protected-map.json` proves237 original unowned working files unchanged. `protected-hunks.json` rechecks the original R4 added hunks exactly; original two previously admitted CameraManager guards retain their recorded round0 qualification.
- `original-artifacts-unchanged-map.json` rehashes all665 round0 artifact entries unchanged. New `artifact-map.json` hashes212 relevant scoped logs/receipts/binaries/test artifacts, excluding disposable Library/cache directories. Failed source maps are recorded in the build receipt; raw failed projects/sources remain available.
- Immutable `sources.zip` contains full candidate/working Task9 source copies, maps and scoped diff; SHA256 `a29cbead9acac4e4f1666841741a38a98522460a0e49ea43e089b37dead55aad`.
- Ignored `prepare.py`, `freeze.py`, `host.cmd` provide repeatable snapshot/candidate/evidence tooling. Existing managed verification runner narrowly expects the new actual8 tests.

## Remaining qualification limits

No new APK was built/installed and no physical timing experiment was run. Round0 APKs/native hashes/screenshots remain historical and cannot qualify this changed native binary. The new first-use regression is covered by production-path host barrier/fence tests plus actual fresh API26 compilation; it is not presented as a hardware replay. Root may decide whether fresh physical correctness evidence is required after review; the bounded timing experiment cap remains respected.

Original actual recognition evidence and unresolved performance concerns remain unchanged: native complete fresh observations around7.858FPS, ageP95 around312ms, no30FPS/complete-hand acceptance, no valid motion recording, private diagnostic style differs from the final renderer. These fixes restore compatibility and retirement correctness without extending those claims.

No owned source/build/Unity/device writer remains. Only the three original September26 user Unity processes remain untouched. Index remains empty. Root must perform fresh scoped specification and quality review before staging/commit or Task10.


## Scoped re-review

Spec compliance: PASS (scoped Task9 fix round1).
Code quality: PASS (scoped Task9 fix round1).

## Scope and verdict

Reviewed I1-I4 and the public dropped-outcome comment against the ten-file original final3-to-fix1 diff. All four Important findings are closed. No new Critical or Important regression was identified in these fixes. This is a scoped re-review, not a new full Task9 review or Task10/final Demo acceptance.

## Closure findings

- **I1 closed.** Actual UPM `Runtime/Demo/VideoPlayerFrameSource.cs:340` restores the explicit public `(Texture,long)` CLR overload and forwards to the four-argument route. The four-argument method remains present; canonical's original four-argument identity is preserved. UPM `Tests/EditMode/InputAdapterTests.cs:99` checks the two-argument identity as well as four arguments. The retained UPM RED2 fails specifically on missing old identity; candidate GREEN passes it.
- **I2 closed.** `runtime/gpu/android/unity_vulkan_plugin.cpp:1465` clears command destination state before real recording; successful Blit/Color command completion sets it (`:1536`, `:1579`). Source-only retirement explicitly clears it (`:1584`), and successful submission initializes the destination only when the submitted command writes it (`:1607`). Thus empty source retirement preserves both a fresh uninitialized destination and an already established destination state. The first real copy uses UNDEFINED, zero source access and IGNORED queue families (`:1484-1490`), restricted to this newly allocated never-written SDK destination under root's authoritative first-use ownership ruling. This does not change imported source ownership. Subsequent real copies retain the EXTERNAL reacquire protocol; the release barriers and sync-fd export/completion remain intact. The new production Android adapter test (`tests/runtime/test_unity_vulkan_android_adapter.cpp:786`) executes actual recording/submission dispatch, checks an incomplete real simulated fence, then checks first destination barriers and a later EXTERNAL/GENERAL reacquire after another source-only retirement. This answers the concrete defect without a new physical timing experiment.
- **I3 closed.** Both `HumanVisionInputAdapter.cs:88` copies configure newly added CPU bridges, while existing bridges use `BindManager`. Canonical `VideoPlayerFrameSource.cs:503` and UPM `:506` replace manager subscriptions without assigning display/fitter. The CPU regression runs the real adapter, source lease, GPU readback callback and manager submission boundary, then verifies the original RawImage/fitter and preview remain bound. The recording session is a submission observer and invents no model results.
- **I4 closed.** Both CPU branches compare analysis RenderTexture allocation dimensions, rather than preview SourceWidth/Height (canonical `VideoPlayerFrameSource.cs:382`, UPM `:385`). Allocation stores width/height in each readback slot; callback row normalization and native submission use those captured values (canonical `:705-715`, UPM `:708-718`). Preview dimensions remain actual source dimensions. The HD regression advances LateUpdate before callback completion, observes 1920x1080 preview while submitting 1280x720/stride5120/3686400 bytes, and verifies the next accepted frame reuses its allocation. No new NCNN CPU readback/fallback or Android hardware RTSP CPU route appears in the fix.
- **Public drop comment closed.** `native/include/humanvision/humanvision_android_gpu.h:85` now distinguishes no SDK AHB copy/inference publication from previously queued Unity normalization requiring a real fence.

## Evidence inspected

Retained raw evidence was read; no tests or builds were regenerated.

- RED2 XML: canonical `out/input/task9-fix1-red2-canonical/tests.xml:2` reports 6/8; failures show null preview texture and width1920 instead of1280. UPM corresponding XML reports5/8 and additionally the missing old CLR overload. Original RED attempts are explicitly harness failures: CPU callbacks did not reach the intended boundary before the EditMode Awake pool setup correction. They are not treated as I3/I4 behavioral RED.
- Clean candidate XML reports8/8, failed0 for canonical and actual UPM (`task9-fix1-candidate-green-{canonical,upm}/tests.xml:2`). Receipts bind both to `out/input/task9-fix1-candidate` and exit0. The tests independently exercise actual CPU machinery and the respective public identities.
- Native RED log records GENERAL versus UNDEFINED and EXTERNAL/family5 versus IGNORED at lines1334-1350. Native GREEN log reports32/32 and includes the new production adapter test at1327. This is simulated Vulkan control-path/barrier/fence evidence, not physical GPU replay.
- Fresh API26 build log ends with successful libhumanvision.so linkage; retained CMake cache has android-26, BUILD_TESTING OFF and GPU_GATE OFF. The audit reports ARM64 dependency closure. Raw symbol evidence contains all three admitted ticket exports and no private Task/Gate export match.

## Qualifications and review discipline

No remaining Important fix is requested. Existing C4996/C4100/C4456 and pinned Unity header encoding warnings remain minor legacy/toolchain qualifications. Existing narrow test names and Task8 managed-local-texture ownership qualification remain as previously recorded; these fixes do not claim to close unrelated acceptance gaps.

No new APK/device replay exists for this changed native binary. Round0 physical observations remain historical, and cannot qualify this changed native binary or establish30FPS, complete-hand acceptance, smooth motion or valid recording. This limits evidence claims; it does not reopen the resolved first-use layout defect whose actual production adapter behavior has focused host coverage and fresh API26 compilation. Final three Demos and clean two-package imports remain Task10 after the reviewed commit.

Root's fresh verification of60 owned working/candidate paths,237 protected paths,665 original unchanged artifacts,212 new artifact hashes and empty index/base69071cd is accepted as controller evidence, not independently rehashed here. Protected R4 hunks remain excluded from the clean candidate.

Read the scoped diff in sequential chunks after the initial output truncated. Focused changed-file reads expanded the cut callback header, release-before-reallocation lifetime guard, EndBarriers, and Export/Complete solely to check captured geometry and preserved release/sync-fd behavior. No broader code crawl, git command, source/index/HEAD mutation, suite rerun or subagent occurred. Only this review file was written.

Managed compilation qualification: round1 fresh managed tests compile Editor D3D11 sources, not a fresh Android IL2CPP build. This scoped managed diff changes the common public overload, common CPU allocation/callback fields, and the existing CPU-route manager-binding call; it adds no Android-conditional route or Android-only signature. BindManager remains internal within the same Demo assembly and its common definition/call are compiled by the actual canonical and UPM Editor runs. No concrete uncompiled Android conditional/call visibility regression was identified. Task10 must perform actual fresh Android full build/import acceptance; neither old APK nor old conditional-compilation evidence is assigned to the new bytes.
