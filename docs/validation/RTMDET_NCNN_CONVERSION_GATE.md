# C2 RTMDet Nano to ncnn conversion gate

Status: **C2 FAILURE / PLAN IMPASSE (2026-09-25)**. RTMDet graph and
four-image golden parity passed, but its full warmed detector P95 exceeded the
33.33 ms TopDown period. The sole approved NanoDet-Plus-m 320 substitution
also exceeded the period after one fixed person/DFL output crop. No production
detector is selected. C3 is not authorized pending an explicit design ruling.

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
optimization or third model has been attempted. NanoDet was **not** granted
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
contract. Keep C2 blocked pending an explicit design ruling; do not infer
production acceptance from either model's successful Vulkan execution.
