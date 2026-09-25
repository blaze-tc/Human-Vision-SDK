# C2 RTMDet Nano to ncnn conversion gate

## Revision 3 Task 1: provisional local detector eligibility (2026-09-25)

Revision 3 authorizes a **local evaluation detector** ahead of the detached
keyframe scheduler. This is a correctness and Vulkan eligibility result, not a
production performance pass. The older Revision 2 every-frame full-detector
P95 values (38.7561, 39.6981, 39.3521 ms) remain failed and unchanged.
There is no public weight redistribution or Release approval.

The pinned checkpoint was re-exported using
`.venv-reference/Scripts/python.exe -m tools.models.ncnn.export_rtmdet_nano
--checkpoint out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth
--vendor-root out/c2-vendor --output out/c2-local-detector/rebuild/rtmdet-nano.onnx
--expected-checkpoint-sha256 05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1
--expected-onnx-sha256 3c4a7a1a2fb500678d7b01ebef87c119ce85626667aaf562109d3622b8abeed5`.
The exporter verified the clean pinned OpenMMLab source checkouts and produced
the same ONNX bytes. Pinned `onnx2ncnn` and `ncnnoptimize` regenerated the
316-layer optimized raw graph and FP16 weights:

```powershell
& out/c2-onnx2ncnn-build/onnx/Release/onnx2ncnn.exe out/c2-detector/rtmdet-nano.onnx out/c2-local-detector/rebuild/raw.param out/c2-local-detector/rebuild/raw.bin
& out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe out/c2-local-detector/rebuild/raw.param out/c2-local-detector/rebuild/raw.bin out/c2-local-detector/rebuild/optimized.param out/c2-local-detector/rebuild/optimized.bin 65536
.venv-reference/Scripts/python.exe -m tools.models.ncnn.finalize_rtmdet_eval
```

The finalizer checks the source/tool hashes, exact optimized graph bytes,
all seven original shape lines, the corrected output hash, RGB 320x320 FP16
pack1 input, `cls`/`bbox` outputs, and full C1 graph/provenance. It writes
only ignored `out/c2-local-detector/{model.param,model.bin,model.json}` with
`local_evaluation_only=true`. The local profile retains the fixed 0.35 person
score threshold. SHA-256: optimized raw param
`8fd7ccb55c9e28161686e748fb0dd6bd042716d45f614f837586c9a126e35346`,
final param `9a4a89da2de4298427255950e58943f670a9e18a6d69b720270070741978b2b3`,
weights `4329c052c86a53fd2f213b874f199a87755df6601e40bb7a45011b280dba42da`,
ONNX `3c4a7a1a2fb500678d7b01ebef87c119ce85626667aaf562109d3622b8abeed5`,
checkpoint `05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1`.

The full C1 audit passed against the local manifest with checkpoint, ONNX,
param, bin, official image, and both hashed converter paths. The fixed
four-image C1 comparator and independent raw-to-JSON check passed 1/1/2/0
persons. On the OnePlus 9 Pro / Adreno 660, the local model was rerun with
explicit FP16 pack1 `VkMat` (`c=3`, `elempack=1`, `elembits=16`). An audited
diagnostic runner checked all **316/316** loaded ncnn layers had
`support_vulkan=true`, then produced `cls` and `bbox` FP32 pack1 tensors
byte-identical to the previously hashed C2 golden on all four images. The
runner source is ignored `out/c2-ncnn-runner/runner_exact.cpp`; rebuilt binary
SHA-256 `f4ad10280b92451c8e25e056e4c33e500f31e1c45153d48ee629127c565104e8`.
The GPU input and output audit is a model eligibility check; integrated B5
camera preprocessing and scheduling remain later tasks.

Host verification: focused reference 5/5, full reference 46/46, full native
197/197, `tools.models.ncnn.diagnose_c2_failure` 4/4, Android ARM64/API26
native build, architecture guard and `git diff --check` passed. The device
runner's raw output comparisons passed all eight tensors. No Task 2 ModelPack
or production profile was created.

---

Status: **C2 FAILURE / BOUNDED SEARCH EXHAUSTED (2026-09-25)**. RTMDet graph and
four-image golden parity passed, but its full warmed detector P95 exceeded the
33.33 ms TopDown period. The sole approved NanoDet-Plus-m 320 substitution
also exceeded the period after one fixed person/DFL output crop. No production
detector is selected. A subsequent user-approved two-candidate search also
failed its early performance gate; C3 remains unauthorized pending a new
design ruling. The candidate sections below preserve the sequence and evidence.

## Authority and timebox

Revision 2 of `docs/superpowers/specs/2026-09-13-android-vulkan-ncnn-production-runtime-design.md`
and Task C2 of the ordered implementation plan control this gate. C1's
`audit_ncnn_graph.py` requires `--manifest`, `--param`, `--onnx`,
`--checkpoint`, `--bin`, and `--fixture`; the short audit command printed in
the C2 plan omits required arguments and is stale. Optional tool paths trigger
hash verification of the actual executable files.

The official OpenMMLab checkpoint was downloaded from
`https://download.openmmlab.com/mmpose/v1/projects/rtmpose/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth`
to ignored `out/c1-source-cache/`. At **2026-09-25 16:27:52 +08:00**, the
42,240,500-byte file was verified with SHA-256
`05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1`.
The 24 engineering hour / three working day timebox began at that checkpoint.
The baseline attempt plus at most two focused corrections are allowed.

## Attempt ledger

| Attempt | Time (+08:00) | Command / result | Failing operator or gate | Correction count |
| --- | --- | --- | --- | --- |
| Baseline export | 2026-09-25 16:27–16:32 | `python -m tools.models.ncnn.export_rtmdet_nano --checkpoint out/c1-source-cache/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth --vendor-root out/c2-vendor --output out/c2-detector/rtmdet-nano.onnx --expected-checkpoint-sha256 05d8511e7b3fabc62e27d2f624179e004ad14ee63a86ca9d9d22c88f3db0eee1` passed static ONNX audit; opset 17; `cls` `[1,2100,1]`, `bbox` `[1,2100,4]`; ONNX SHA-256 `3c4a7a1a2fb500678d7b01ebef87c119ce85626667aaf562109d3622b8abeed5`. | None at export. ncnn conversion, Vulkan, golden and P95 pending. | 0 |
| Baseline conversion | 2026-09-25 16:38 | `pwsh -NoProfile -File tools/models/ncnn/convert_to_ncnn.ps1 -Role detector ... -OutputDirectory out/c2-detector/baseline` produced 316-layer raw/optimized ncnn graphs and a 1,990,328-byte FP16 weight file. The C1 graph audit rejected the optimized param and deleted the draft manifest. | `ValueError: ncnn input shape mismatch: {} != (320, 320, 3)`; the official `onnx2ncnn` output is `Input in0 0 1 in0` without static dimensions. The graph also contains six `Reshape` layers with `1=-1`, which violate the static gate. | 0 |
| Targeted correction 1 | 2026-09-25 16:39 | In the ignored candidate `model.param`, set `Input` to `0=320 1=320 2=3` and the six `Reshape` height dimensions to `1600,400,100` for the three classifier heads and `1600,400,100` for the three box heads, derived from the audited ONNX `[1,2100,1]` / `[1,2100,4]` outputs and 40²+20²+10² grid. `audit_param(..., ('cls','bbox'), (320,320,3))` then passed, with exactly `bbox,cls` terminal blobs. No layer was added or replaced. | Static shape information dropped by `onnx2ncnn`; targeted param rewrite only. | 1 |
| Detector-only input contract ruling | 2026-09-25 16:59–17:03 | Device first-convolution comparison proved forced pack4 changes the three-channel graph. Per Revision 2 §8.3 model-supported packing and C1's `where supported`, detector `Extractor::input` is FP16 pack1; Body26 remains FP16 pack4. The contract regression failed before the one-line C1 contract fix, then 30/30 focused tests passed. Pinned exporter reproduced the identical ONNX SHA-256; regenerated manifest and golden provenance passed the full C1 audit and four-image comparison. | Unsupported detector pack4 layout; no second graph correction or custom layer. | 1 |

After correction 1, the full `audit_ncnn_graph.py` CLI with manifest, param,
ONNX, checkpoint, bin, fixture, and both host tool paths **passed**. The
316-layer graph contains only allowlisted layers, exactly `cls` and `bbox`
terminal blobs, and no custom layer or persistent ncnn-core change. The
candidate was loaded and executed by ncnn Vulkan on the OnePlus 9 Pro
(Adreno 660). A first temporary harness run inferred successfully but returned
exit 134 because the harness destroyed ncnn's Vulkan instance before its
stack-owned Net destructor; moving process cleanup to OS exit fixed the
harness, and the same graph then returned exit 0. This was a harness lifecycle
error, not a graph correction.

## Detector golden results (2026-09-25)

Identical preprocessed RGB 320x320 input tensors were fed to the pinned ONNX
reference and the converted FP16 ncnn Vulkan model. The fixed image corpus is
one official MMDeploy person image, the repository's one-person and two-person
video frames, and an official MMDeploy street negative. Their exact image,
input tensor, raw output, and model hashes are bound in ignored
`out/c2-detector/golden/index.json`; each fixture has a C1 SHA-bound
`model-golden.json`. The scored, source-space boxes use the profile's fixed
0.35 person threshold and NMS IoU 0.6. The C1 comparator passed for each
fixture and the independent repeat ncnn run matched raw output bytes exactly.

| Image | Reference/candidate persons | Minimum matched IoU | Maximum score error |
| --- | ---: | ---: | ---: |
| MMDeploy human-pose | 1 / 1 | 0.9993465 | 0.0011637 |
| Repository one-person frame 0 | 1 / 1 | 0.9997868 | 0.0011179 |
| Repository two-person frame 2 | 2 / 2 | 0.9992647 | 0.0010081 |
| MMDeploy street negative | 0 / 0 | n/a | n/a |

The explicit `.venv-reference/Scripts/python.exe -m
tools.models.ncnn.diagnose_c2_failure -v` diagnostic passed 4/4 using the
declared FP16 pack1 contract, pinned local checkpoint, ONNX, model files,
and device evidence. It independently decodes every hashed raw ONNX/ncnn
tensor into source-space post-NMS JSON and checks exact equality with each
recorded JSON file, then applies the C1 person-count/IoU/score comparator.
It enforces corpus roles and counts: official 1, video one-person 1, video
two-person 2, street negative 0. A forged empty official JSON is rejected.
The full C1 audit CLI with both hashed host tool paths also passed after
manifest regeneration. Large source/evidence files stay under ignored `out/`;
a clean checkout must regenerate them before invoking this diagnostic.
The default `tests/reference` suite does not discover this hardware gate.

## Snapdragon 888 timing and resolved input contract

An initial ncnn Vulkan harness measured 20 warm-up plus 100 graph runs from a
CPU `Mat`, using two FP32 output extractions: P50 34.6905 ms, P95 37.1169 ms.
That measurement includes CPU tensor upload and output downloads, so it does
**not** by itself trigger the Revision 2 GPU-native detector exit condition.

A second harness explicitly uploaded the input once per iteration, executed
the graph on a pre-uploaded `VkMat`, and downloaded only `cls` and `bbox`.
With 20 warm-up plus 100 measured iterations, upload P50/P95 was
3.0996/3.9026 ms, graph-only P50/P95 28.8457/33.9745 ms, and output download
P50/P95 3.1465/4.3179 ms. A repeat run measured graph-only P50/P95
29.4226/32.2134 ms. Since the two graph P95 values straddle the full
33.33 ms period and the production input layout was then unresolved, no
performance exit was declared from these temporary runs.

The original C1 model contract demanded FP16 **pack4** at `Extractor::input`
for the static 3-channel RGB model. A 3-channel normalized upload followed by
`convert_packing(...,4,...)` retained **elempack=1, c=3, elemsize=2** on the
Adreno 660. The B5 production preprocessor instead normalizes the same RGB
pixels to **four channels**, filling the fourth with zero, then converts to
pack4. Reproducing this exact path yielded `VkMat` **elempack=4, c=1,
elemsize=8** and the uploaded data echoed back with first-three-channel max
error 0.00194 (FP16 rounding), fourth channel exactly zero. Two exact-pack4
graph-only timing runs measured P50/P95 **29.2115/34.4239 ms** and
**30.4696/34.3317 ms**; separate upload P95 was 6.3124/6.2219 ms, and
two-output download P95 4.3083/4.4048 ms. The exact-pack4 graph P95 exceeds
the whole 33.33 ms frame period, but that input currently produces invalid
model outputs and is not usable performance evidence for a valid detector.

The divergence begins at the **first convolution**: the same raw RGB input
through explicit FP16 pack1 versus four-channel zero-padded FP16 pack4 yielded
different 160x160x8 stem tensors (max absolute difference 24.71875, mean
2.96378, correlation 0.3901). Explicit GPU output conversion to FP32 pack1
on the pack1 path reproduced the earlier golden raw tensors bit-for-bit;
therefore the output download/parser is not the cause. The first convolution's
weight count is 216 = 8 output channels × 3 RGB channels × 3 × 3 kernel.
The official ncnn graph accepts pack4 as a tensor shape but does not compute
the same 3-channel model with it. The approved detector-only ruling sets
`Extractor::input` to **FP16 pack1**, matching the actual supported graph;
Body26 remains pack4. This contract edit is separate from graph correction 1.
No second graph correction or NanoDet substitution occurred.

With explicit FP16 pack1 `VkMat` (`c=3`, `elempack=1`, `elemsize=2`), output
conversion to FP32 pack1, and 20 warm-up plus 100 measured iterations per run,
three consecutive Snapdragon 888 runs gave:

| Run (+08:00) | CPU upload P50/P95 ms | Vulkan graph-only P50/P95 ms | Two-output download P50/P95 ms |
| --- | ---: | ---: | ---: |
| 2026-09-25 17:02 run 1 | 2.9083 / 3.6368 | 21.5563 / 22.7339 | 2.0703 / 2.5388 |
| 2026-09-25 17:02 run 2 | 4.9833 / 5.8514 | 29.2773 / 31.5753 | 3.3184 / 3.9032 |
| 2026-09-25 17:03 run 3 | 5.1037 / 6.0534 | 30.2237 / 32.2888 | 3.4167 / 4.0973 |

The slowest valid graph-only P95 is **32.2888 ms**, below the 33.33 ms
TopDown frame period. Upload and download are reported separately because
this temporary runner starts from CPU memory and cannot represent the full
GPU-native TopDown path. C6 must measure the integrated detector, pose,
transfer, and rendering budget. The invalid pack4 P95 does not trigger exit.
The diagnostic runner binary SHA-256 is
`a9f1eeb450cf5919a91a8ef61c8b805723e5d45626744e7546213e637197f851`
(`out/c2-ncnn-runner-android/c2_ncnn_bench_split`, ignored). It was run as
`adb -s e7c07019 shell /data/local/tmp/hv-c2/c2_ncnn_bench_split
/data/local/tmp/hv-c2/model.param /data/local/tmp/hv-c2/model.bin
/data/local/tmp/hv-c2/input.fp32` three times; the input tensor is the
hash-bound official golden input. This runner only measures C2 and is not
production runtime code. The current B5 preprocessor still submits a padded
pack4 detector tensor; B5 integration must adopt the accepted detector-only
pack1 contract before the production path can reproduce C2 parity.

Pinned OpenMMLab source revisions: MMPose
`5408bc76f5b848cf925a0d1857899011d8c5b497`, MMDetection
`fe3f809a0a514189baf889aa358c498d51ee36cd`, MMDeploy
`bc75c9d6c8940aa03d0e1e5b5962bd930478ba77`. Source checkouts are
temporary and must pass C1's clean-tree guard before export. ncnn source is
the project-pinned 20260526 release, commit
`e54f7b1f88434e1d844ea0551b880a1cfb079ce1`.

Host tool provenance: the official ncnn 20260526 Windows VS2022 archive is
70,623,601 bytes, SHA-256
`5ed388b134c50ba8eb2e6605f3a16f692f26598d45113bc9794aa4857d691e26`.
Its x64 `ncnnoptimize.exe` SHA-256 is
`40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7`.
The release package contains no `onnx2ncnn.exe` and pinned ncnn's top-level
`tools/CMakeLists.txt` does not include the remaining `tools/onnx` directory.
The attempt to build the ncnn CMake target failed with
`ninja: error: unknown target 'onnx2ncnn'`. With the parent task's explicit
ruling, the unmodified `tools/onnx/onnx2ncnn.cpp` was independently built
against official Protobuf v21.12, checkout
`f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c`, using MSVC v143 and Ninja
Multi-Config. The host `onnx2ncnn.exe` SHA-256 is
`b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb`.
The temporary wrapper CMake file is under ignored `out/c2-onnx2ncnn/`; ncnn
core, C1 tooling, and production source were not edited.

## C2 exit decision: complete detector latency

The RTMDet `.param` and `.bin` passed the full C1 graph/provenance audit
and ran wholly on ncnn Vulkan without CPU fallback. Four fixed images passed
the C1 person count, IoU at least 0.95, score error at most 0.01,
no-missed-person, and repeat gates. That is a **golden pass only**. The
earlier graph-only P95 omitted the required output downloads and host
decode/NMS, so it cannot select a production detector. A tracked diagnostic
runner, `tools/models/ncnn/c2_detector_benchmark.cpp`, measured the **same
iteration** from pre-uploaded real FP16 pack1 `VkMat` through graph, both
FP32 pack1 downloads (`cls` 2,100 floats; `bbox` 8,400 floats), source-grid
decode and person NMS. Its official golden input SHA-256 is
`ed59a51702487b76dfeeac63f93cea4ecf426f278a2cb96f72970e8efde8cb61`.
Each device run used 20 warm-up and 100 measured iterations. All 100 decoded
frames in each run contained one person. The runner binary SHA-256 is
`a9f1eeb450cf5919a91a8ef61c8b805723e5d45626744e7546213e637197f851`.

| RTMDet run | Graph P95 | Download P95 | Decode/NMS P95 | Paired total P50/P95 (ms) | Raw log SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| `out/c2-detector/bench/full-detector-run1.log` | 34.0307 | 4.4459 | 0.5922 | 33.8366 / **38.7561** | `3fd5f5561b4777dbbad0b707eafd485a658ba23281078ec936074506968cbe0a` |
| `out/c2-detector/bench/full-detector-run2.log` | 34.8037 | 4.4266 | 0.5745 | 32.9529 / **39.6981** | `5e94f63fa9a54e9079cfb846827cb85a0343c5e8006daa8b8996ca9881773646` |
| `out/c2-detector/bench/full-detector-run3.log` | 34.5226 | 4.4812 | 0.5293 | 32.6816 / **39.3521** | `cf62c47a47c0cbe8072c650a81a205fe755954f4277fce30395a7d47ea313be3` |

The RTMDet performance exit fired. No further RTMDet graph rewrites were
attempted; only the approved NanoDet-Plus-m 320 substitution was evaluated.

## Sole approved NanoDet-Plus-m 320 substitution: failed performance gate

The official [NanoDet repository](https://github.com/RangiLyu/nanodet/tree/be9b4a9001d7f9b6fc89c2df31ae8d428e35b4f0)
at commit `be9b4a9001d7f9b6fc89c2df31ae8d428e35b4f0` provides the
320 configuration and [ncnn demo](https://github.com/RangiLyu/nanodet/blob/be9b4a9001d7f9b6fc89c2df31ae8d428e35b4f0/demo_ncnn/nanodet.cpp).
Official [v1.0.0-alpha-1 release](https://github.com/RangiLyu/nanodet/releases/tag/v1.0.0-alpha-1)
assets [`nanodet-plus-m_320.onnx`](https://github.com/RangiLyu/nanodet/releases/download/v1.0.0-alpha-1/nanodet-plus-m_320.onnx) (4,793,615 bytes, SHA-256
`4f12723cce3d48e47ca92cb925ba74d97a965c069208edca660bbb9f7ce2c610`)
and [`nanodet-plus-m_320_checkpoint.ckpt`](https://github.com/RangiLyu/nanodet/releases/download/v1.0.0-alpha-1/nanodet-plus-m_320_checkpoint.ckpt) (35,470,891 bytes, SHA-256
`f4c6080f3ef35a64c3030d559438b436462f227ce29700bca71327203e42dea2`)
remain in ignored `out/c2-nanodet/`. The official ONNX has input `data`
`[1,3,320,320]` and output `output` `[1,2125,112]`: 80 COCO scores plus
4×8 DFL distribution channels at strides 8/16/32/64. The official ncnn demo
uses BGR mean `[103.53,116.28,123.675]` and norm
`[0.017429,0.017507,0.017125]`; the person label is class 0. The pinned
`onnx2ncnn` and `ncnnoptimize` produced `model.param` SHA-256
`d79e18ecd8595081bb29fdf0c790a47fcea96fbf58598350569a92e71e915b72`
and 2,397,184-byte `model.bin` SHA-256
`400bc25cc522b0fc2c1654d810e55f32b73fffbfe44bf680a709f740d4e3b195`.
The full FP32 output is 952,000 bytes. The real official person image input
tensor has SHA-256
`ecf10db848abf724186a899043095a801dd377bb6640125641111f0a7230d698`.

The baseline ncnn Vulkan graph loaded and produced one decoded person. Its
three 20-warm-up/100-measured paired total P95 values were
**60.8779/60.3232/59.5476 ms**; graph P95 was
37.8418/37.5709/36.7614 ms and full-output download P95 was
23.0849/23.3316/22.9241 ms. The raw logs are
`out/c2-nanodet/bench/full-run1.log` (SHA-256
`67326af864c625aa8663b8b780a93b15416a360c8f4b8877651d29b68666844d`),
`full-run2.log` (`030a49c23d6dfb30a4b1aa45b018bb4dc9fea1277460dfccbe82098f949e85d2`),
and `full-run3.log` (`c0cfd2e1be55bfcde1ec77678e6e10d928d4903227090216095a8a0d9c05e1fb`).

One bounded detector-only graph experiment appended a static `Split` plus
two `Crop` layers after the official `output`, yielding only person class-0
score `[2125,1]` and 4×8 DFL `[2125,32]` as FP32 downloads (280,500 bytes).
It added no custom layer or ncnn-core change. The candidate param SHA-256 is
`4b7b4dad50107a348e3f5d7b1dd9f26947c6c1c53cb86f78b73cc7e806dfd922`;
weights are unchanged. The tracked runner
`tools/models/ncnn/c2_nanodet_benchmark.cpp` decoded class-0 scores, softmax
DFL distances, and person-only NMS on the host; every measured frame yielded
one person. Its binary SHA-256 is
`ffebaeea82e5bd4aeee5423e86cccfc9400d8dd0e619c8ea3c526bed50db0283c`.

| NanoDet crop run | Graph P95 | Download P95 | Decode/NMS P95 | Paired total P50/P95 (ms) | Raw log SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| `out/c2-nanodet/bench/person-run1.log` | 38.6518 | 9.4408 | 0.2575 | 43.1552 / **48.2666** | `db22455d984a72f99bcd51a7b4adc93da155768ebba14414d5142b9cb6729b82` |
| `out/c2-nanodet/bench/person-run2.log` | 39.9877 | 9.3572 | 0.3018 | 43.0327 / **49.5960** | `af7b1c389c3eea054af07a969bd781888fa2cb3124f13ad97204daaeb8e0bd76` |
| `out/c2-nanodet/bench/person-run3.log` | 39.8066 | 9.2951 | 0.2558 | 42.0444 / **49.0909** | `f67dcca913b7750d09872a8aa0f4f291f45e27205c5e52e8c5c5fb5265283673` |

The only approved substitute also fails 33.33 ms. No further detector
optimization or third model had been attempted **at that original impasse,
before the later user-approved candidate screening**. NanoDet was **not** granted
golden parity or human-recall acceptance: a single decoded person in this
latency fixture is diagnostic, not the four-image gate.
The three crop runs used
`adb -s e7c07019 shell /data/local/tmp/hv-c2/nanodet/c2_nanodet_full
/data/local/tmp/hv-c2/nanodet/person.param
/data/local/tmp/hv-c2/nanodet/model.bin
/data/local/tmp/hv-c2/nanodet/input.fp32` after pushing the hashed files.
The original RTMDet model files were moved intact to ignored
`out/c2-detector/failure-pack/`; the production
`modelpacks/precision-t-26-ncnn-fp16/detector/` has no selected assets.
`tools/models/ncnn/diagnose_c2_failure.py` is an explicit failure diagnostic.
It rechecks the raw-bound RTMDet golden, verifies all three hashed RTMDet
paired logs, and verifies the NanoDet checkpoint/ONNX/param/bin hashes plus
all three hashed crop logs and paired P95 exits. It also hashes the actual
ignored `onnx2ncnn.exe` and `ncnnoptimize.exe` against the pinned values above;
a forged converter executable is rejected. It never selects either
model for production. Its external evidence stays under ignored `out/`;
a clean checkout must regenerate that evidence before running the diagnostic,
and missing evidence fails closed.

The NanoDet repository's [Apache-2.0 LICENSE](https://github.com/RangiLyu/nanodet/blob/be9b4a9001d7f9b6fc89c2df31ae8d428e35b4f0/LICENSE)
covers its source. No explicit permission for redistribution of these release
weights or COCO-derived trained assets has been established. Likewise,
MMPose's Apache-2.0 source license alone does not establish permission to
redistribute the RTMDet trained weights. **Weight redistribution is an
unresolved release gate**; no detector binary should be published or committed
as an accepted production asset on that basis.

The two permitted detector paths are exhausted under the current performance
contract. At this point C2 was blocked pending a design ruling; the user later
approved one bounded two-candidate search, which also failed. Do not infer
production acceptance from any model's successful Vulkan execution.

## Approved bounded candidate 1: PP-PicoDet-XS 320 COCO (2026-09-25)

The subsequent user-approved C2 extension permits at most two further candidates.
Its 24 engineering hour / three working day limit counts from initial research;
**2026-09-25 18:42:42 +08:00** is the formal recorded upper bound for that
start, not a model acceptance timestamp. This section records the first candidate
and its early performance exit. No production detector was selected.

PaddleDetection's [release/2.9 model source](https://github.com/PaddlePaddle/PaddleDetection/tree/b25522a0f4bde8c80603f3ba5e3472059972e3b5/configs/picodet)
and [official ncnn deployment README](https://github.com/PaddlePaddle/PaddleDetection/blob/b25522a0f4bde8c80603f3ba5e3472059972e3b5/deploy/third_engine/demo_ncnn/README.md)
describe the model family and eight no-postprocess heads. The ignored local
[`picodet_xs_320_coco_lcnet.pdparams`](https://paddledet.bj.bcebos.com/models/picodet_xs_320_coco_lcnet.pdparams)
is 2,921,876 bytes, SHA-256
`0e18cb1a00b45283d4760f2302479ccddc89152ad2f7fa8678a8677be06944b8`.
The ignored official [no-postprocess ONNX](https://paddledet.bj.bcebos.com/deploy/third_engine/picodet_xs_320_coco_lcnet.onnx)
is 2,859,300 bytes, SHA-256
`e83deca19863b127e702ae340ed7f7455334471886f0251413be459142a08aec`.
Its opset is 11; runtime input `image` is `[1,3,320,320]` (legacy ONNX also
lists initializers as graph inputs). Its score outputs are `[1,1600/400/100/25,80]`
and corresponding box-distribution outputs are `[1,1600/400/100/25,32]`.
The official [ncnn demo preprocessing](https://github.com/PaddlePaddle/PaddleDetection/blob/b25522a0f4bde8c80603f3ba5e3472059972e3b5/deploy/third_engine/demo_ncnn/picodet.cpp)
uses BGR resize to 320², BGR mean `[103.53,116.28,123.675]`, and normalization
`[0.017429,0.017507,0.017125]`. The real C1 official-person fixture was
preprocessed that way into ignored `out/c2-picodet/input.fp32`, SHA-256
`e5746224aad2abd278223e9c00609c7edd831a9bca48652cdb373c986065b9ac`.

The pinned `onnx2ncnn.exe` SHA-256
`b61a55f852c603b84aa417e91f6e6c5c7e119e673dc06536930d1d5cd7f4fefb`
converted this ONNX with exit 0; `ncnnoptimize.exe` SHA-256
`40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7`
optimized it with exit 0 and FP16 weight storage flag `65536`. The unmodified
optimized `model.param` SHA-256 is
`91a5410c3da2ed8dc9215c41cb20ea4e9b4dccf9dfb8f46e620d3b2b88e67af7`;
`model.bin` is `04f5e5f675a5e28ae29d321fb3164f363e7e87e1827325ab3529db262093fe35`.
The original param has an input with no static shape. A bounded diagnostic copy,
`person.param` SHA-256
`fcc80710a5754ed5e41641a4800c05b3aa139141ab571539602dc5b870ad8846`,
adds four `Crop` layers to keep only COCO person class-0 scores while retaining
all four 32-channel DFL outputs. No weights, core library, or production pack
were changed. This is **not** a C1 graph-contract pass: C1 currently requires
the RTMDet `in0` and `cls`/`bbox` contract, which PicoDet does not have.

The ignored Android runner `out/c2-picodet/build/c2_picodet` SHA-256
`68cc9fbb56d3fc4147066b2f96861d831abd77392eb10fc60f35bb8a4c173727`
used ncnn 20260526 `VkMat` input with FP16 pack1 on OnePlus 9 Pro / Adreno 660,
requested eight `VkMat` outputs, and downloaded FP32 tensors. It returned exit
0 with the expected eight output dimensions. This establishes executable GPU
input/output, but does not certify the strict no-CPU-fallback requirement. Each
run had 20 warm-up and 100 measured frames from the same real-image tensor.

The tracked runner source (the ignored original with only its extra terminal
blank line removed) is
`tools/models/ncnn/picodet_c2_benchmark.cpp`, SHA-256
`0b30ace0365b8f68a329961a776757fbb5c7f27da3372e9e4857f33e70b4168f`.
The ignored original source SHA-256 is
`2caa76454dbbf73420017153bf5a889f0803a15102d571990a4a720663e0de5c`.
The tracked runner's CMake recipe is
`tools/models/ncnn/picodet_c2_benchmark/CMakeLists.txt`.
With the C1 ncnn 20260526 Android arm64 installation and Unity Android NDK
present, it was rebuilt from the tracked files by:

```powershell
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' -S tools/models/ncnn/picodet_c2_benchmark -B out/c2-picodet/rebuild -G Ninja -DCMAKE_MAKE_PROGRAM='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_TOOLCHAIN_FILE='D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK/build/cmake/android.toolchain.cmake' -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -Dncnn_DIR="$(Resolve-Path out/ncnn-20260526/android-arm64-api26/install/lib/cmake/ncnn)"
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build out/c2-picodet/rebuild -j 4
```

Both commands exited 0 after the tracked source edit. The independent rebuilt
binary SHA-256 is
`dcb66d3acfea6e15671bc73b0925ff976429f6bfe6dc1eccb7402693edf158c`;
the original benchmark binary hash above remains the identity of the logged
device runs; it was compiled from the ignored original source. The two binary
hashes differ with their build directories.

| Output path | Graph P95 | Download P95 | Paired graph + download P50/P95 (ms) | Raw log SHA-256 |
| --- | ---: | ---: | ---: | --- |
| Full eight heads, `out/c2-picodet/run1.log` | 29.4750 | 23.1972 | 45.8271 / **52.1816** | `6e10a67bf89fb2e95385f2315f7d38c31b7cb5b973f37318a4c5b9a8d5efd4a3` |
| Person scores + four DFL heads, `person-run1.log` | 32.4209 | 9.8632 | 36.2186 / **42.0377** | `b3d17d7fa078c4bd0186cf129f33abbf256f139d66cc5237a55d381a4f54a24b` |
| Same path, `person-run2.log` | 33.9311 | 10.1223 | 36.3395 / **42.8649** | `98c4a283bafcdbdc5a0f72c1895292f61166888ca199924f7415ffecee35ae13` |
| Same path, `person-run3.log` | 33.0467 | 10.5643 | 36.0608 / **43.2690** | `89e276ecd5c1ff17e3ae1b88ddf38cc9af562ebe99dbb58744e6ba89c3c8ad9f` |

The paired measurement excludes the required DFL decode and person NMS. All
three cropped-output P95 values already exceed the 33.33 ms **entire TopDown
period**, so this candidate cannot leave any time for pose inference. The early
performance exit stops PicoDet here; four-image golden parity and person recall
were not attempted and must not be claimed. Weight redistribution rights remain
unverified; its binaries stay under ignored `out/c2-picodet/`.

## Approved bounded candidate 2: original MobileNet-SSD VOC 300 (2026-09-25)

**Early performance exit; C2 remains failed.** The second and final candidate
was the original [chuanqi305/MobileNet-SSD](https://github.com/chuanqi305/MobileNet-SSD)
at commit `bb17b6c3eef36d80be441ae8e5339be66e8e3b7a`, not a separately
published ncnn `.bin`. The repository commits both `deploy.prototxt` and
`mobilenet_iter_73000.caffemodel`, with a repo-level
[MIT LICENSE](https://github.com/chuanqi305/MobileNet-SSD/blob/bb17b6c3eef36d80be441ae8e5339be66e8e3b7a/LICENSE).
The license hash is
`7bc9bdb72a3f18e5c9151498cd90ae7d4de472f632d76b13606cda39934aaccc`;
the prototxt (47,769 bytes) hash is
`f32074dda8295f8722b08c184c31bcea3792c7d697812616587290387adb198b`;
the original Caffe model (23,306,119 bytes) hash is
`52eed8be80522c152a17fb56740de705b79881bde1a167e0e747310523685fc7`.
The original `demo.py` specifies BGR bilinear resize to 300², subtract 127.5,
multiply 0.007843, then CHW; its `CLASSES` puts VOC person at index 15.
The repository says the training used MS-COCO and VOC0712. Its MIT source
license does not by itself establish rights to redistribute the trained model
or underlying training data; this remains a release gate if the model is ever
reconsidered.

The pinned ncnn 20260526 host `caffe2ncnn.exe` SHA-256 is
`948fb99b64adb17b5b1ab4fd863ee0c070a3b766201ce06d05bbd521de46c70d`.
It converted the **original** Caffe files with exit 0 to `raw.param` SHA-256
`652f339dca4fd7afe43532d565f78e44a7d7ec63dd9ce4375d60d5c264cce30d`
and `raw.bin` SHA-256
`8d530aa80bcfd95e590fbcc24ede7c62994efa90aafba041004a2f2edbd7b0d2`.
The converter's raw param omitted the explicit top-level Caffe `data` input and
its seven-way split while counting the implicit input in the header. The tracked
`mobilenetssd_c2_prepare.py` makes those explicit with static `[3,300,300]`
input and removes the terminal `DetectionOutput`, which has no Vulkan
implementation in the pinned ncnn source. It does not alter any model weights.
Its `gpu.param` SHA-256 is
`dbe6bfba0b6285fa561c5a42ce1a72d20e1dd6252897e829e8d6100cf2fcb50f`.
The pinned `ncnnoptimize.exe` SHA-256
`40ffdd4f11d0823ccb665d6dc2e421b3a64e1e1945677e95172e3875a0601ac7`
ran with flag `65536` (FP16 weight storage) and exit 0. The resulting
`optimized.param`/`optimized.bin` SHA-256 values are
`78140f4ac169ff434e33415c8092f853a5f771b764eb7cd209e55a4ccd38f633`
and `8f8cbe4ad32c97f0ede51608f5b5a16b7fa7e2b3481ae348e13c9205ff6ba0f0`.
The final GPU graph exposes `mbox_loc` (7,668 floats),
`mbox_conf_flatten` (40,257 floats), and `mbox_priorbox` (7,668×2 floats)
for host SSD decode/NMS. All remaining compute layer types have Vulkan
implementations in the pinned ncnn source; the `Input` pseudo-layer is the
only type without a `*_vulkan.cpp`. **A runtime per-layer CPU-fallback trace
was not completed**, so strict wholly Vulkan execution is not certified.

Reproduction from the worktree root (all large assets and logs are ignored):

```powershell
git clone https://github.com/chuanqi305/MobileNet-SSD.git out/c2-mobilenetssd-source
git -C out/c2-mobilenetssd-source checkout bb17b6c3eef36d80be441ae8e5339be66e8e3b7a
New-Item -ItemType Directory -Force out/c2-mobilenetssd | Out-Null
& out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/caffe2ncnn.exe out/c2-mobilenetssd-source/deploy.prototxt out/c2-mobilenetssd-source/mobilenet_iter_73000.caffemodel out/c2-mobilenetssd/raw.param out/c2-mobilenetssd/raw.bin
& .venv-reference/Scripts/python.exe tools/models/ncnn/mobilenetssd_c2_prepare.py out/c2-mobilenetssd/raw.param out/c2-mobilenetssd/gpu.param
& out/c2-ncnn-host/ncnn-20260526-windows-vs2022/x64/bin/ncnnoptimize.exe out/c2-mobilenetssd/gpu.param out/c2-mobilenetssd/raw.bin out/c2-mobilenetssd/optimized.param out/c2-mobilenetssd/optimized.bin 65536
& .venv-reference/Scripts/python.exe tools/models/ncnn/mobilenetssd_c2_input.py out/c2-detector/golden/official/image.png out/c2-mobilenetssd/input.fp32
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' -S tools/models/ncnn/mobilenetssd_c2_benchmark -B out/c2-mobilenetssd/build -G Ninja -DCMAKE_MAKE_PROGRAM='D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe' -DCMAKE_TOOLCHAIN_FILE='D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK/build/cmake/android.toolchain.cmake' -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -Dncnn_DIR="$(Resolve-Path out/ncnn-20260526/android-arm64-api26/install/lib/cmake/ncnn)"
& 'D:/Microsoft Visual Studio/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' --build out/c2-mobilenetssd/build -j 4
```

The input image SHA-256 is
`7a090e3befceef2fe0db7e8b8a2a2ec03782e8e133a4738c027e3f6f23afac31`;
the preprocessed 1,080,000-byte tensor SHA-256 is
`1da217aea45cb2bc91fae9b1bbe303be3bb67e97622de2a4c37ee79cc418e1d5`.
The tracked runner source is `tools/models/ncnn/mobilenetssd_c2_benchmark.cpp`
(SHA-256 `ee3c4b77c8e158ffad98e22c213d8f4a618a4ba10292b0f99405774c087fe9b2`);
its CMake recipe SHA-256 is
`0261aa0333163b1f511b5fb6695640a12d2f0f915022d6fc83ea246d6b010ede`.
The Android arm64 binary SHA-256 is
`1a2dfe38e496eb4a5cf507b3b277f0e8517e849375355d6edd5f4b20460c6279`.
On the authorized OnePlus 9 Pro / Adreno 660 it printed `input,c=3,pack=1,bits=16`
and the expected three FP32 output sizes. The real image was uploaded once to
`VkMat` before timing. Each run performed 20 warm-up iterations followed by
100 measured iterations, each timing graph submission/wait plus all three
required output downloads. Input upload, SSD decode, person NMS, pose, and
AHB bridge were **excluded**. The device invocation after pushing the four
files to `/data/local/tmp/c2-mobilenetssd/` was:

```text
c2_mobilenetssd optimized.param optimized.bin input.fp32
```

| Optimized run | Paired P50 / P95 / maximum (ms) | Raw log SHA-256 |
| --- | ---: | --- |
| `out/c2-mobilenetssd/optimized-run1.log` | 46.0773 / **51.8276** / 52.5539 | `dbea63cc604430ba189731e8d323c11c61353312e762893ac830c5f5ccca4ca5` |
| `optimized-run2.log` | 34.8373 / **36.1623** / 36.9476 | `ec0c1c464f144210409f86662354b371b46f13ddfeaeece0b52543f35fcbc057` |
| `optimized-run3.log` | 39.9384 / **46.6351** / 48.0527 | `e3aa138958ed3bd723b479542b70d90d6910c22e6dd4ada9c0e34e9ff6da6be7` |

All three **lower-bound** P95 values exceed the entire 33.33 ms TopDown
period. Even the fastest leaves no time for SSD host decode, NMS, pose, bridge,
or tracking. The candidate was stopped under the early exit. Four-image
1/1/2/0 person count, C1 reference parity, runtime CPU-fallback audit, and
weight redistribution were **not passed**. No production ModelPack asset was
selected, and C3 remains unauthorized after both bounded candidates failed.
