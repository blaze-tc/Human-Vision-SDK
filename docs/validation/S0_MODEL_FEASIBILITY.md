# S0 model feasibility: body, hands and RK3588

Research date: 2026-09-07. Status: **candidate selection evidence only; no model downloaded, exported, converted or benchmarked in this task.**

This report follows `SDK_SKELETON_REQUIREMENTS.md` and retains `MODEL_MANIFEST.md` as the D0 regression baseline. The new target is independent SDK recognition on Windows RTX 2060 / Ryzen 7 4800H and Android RK3588, 1-8 visible people, each receiving 30 fresh complete body-and-both-hands updates/second. No AzureKinectExamples integration is proposed here.

## Recommended first experiment

Compare RTMPose-s 133-point wholebody against RTMPose-m 133-point wholebody on the same source frames, keeping the existing person detector initially. These provide real inferred hand landmarks and explicit toes in one crop per person. Start with the s model to test the compute budget; use m to establish whether extra capacity meaningfully improves visible hand endpoints. This is an engineering experiment recommendation, not a model selection or performance guarantee.

At eight people, the wholebody path requires 240 person-crop evaluations/second. A serial pipeline has 33.33 ms for all eight crops, detector, transforms, decoder and publication; a 4.17 ms per-person allowance would consume the entire budget before those other stages. Batching can change this calculation but cannot be assumed to give linear speedup. Measure both batch-one and supported batched exports.

Small hands in a 192-pixel-wide full-person crop may lack usable image detail. If this defeats hand accuracy, compare the modular path below using hand crops from the original camera frame. Never treat wrist extrapolation as a detected palm or fingertip. Model output reduction from 133 or 21 joints to the required semantic joints does not itself remove backbone compute.

## Official candidates and exact artifacts

The following links were obtained from the [official MMPose RTMPose model tables](https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose). Artifact URLs are publisher-listed; availability, archive contents and SHA-256 remain download-time checks.

| Candidate | Exact config | Official artifact |
| --- | --- | --- |
| First: distilled RTMPose-s wholebody, 256x192 | [rtmpose-s_8xb64-270e_coco-wholebody-256x192.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/wholebody_2d_keypoint/rtmpose-s_8xb64-270e_coco-wholebody-256x192.py) | [ONNX SDK zip](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-s_simcc-ucoco_dw-ucoco_270e-256x192-3fd922c8_20230728.zip), [checkpoint](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/rtmpose-s_simcc-ucoco_dw-ucoco_270e-256x192-3fd922c8_20230728.pth) |
| Accuracy comparator: distilled RTMPose-m wholebody, 256x192 | [rtmpose-m_8xb64-270e_coco-wholebody-256x192.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/wholebody_2d_keypoint/rtmpose-m_8xb64-270e_coco-wholebody-256x192.py) | [ONNX SDK zip](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-m_simcc-ucoco_dw-ucoco_270e-256x192-c8b76419_20230728.zip) |
| Modular body with foot landmarks: RTMPose-s Halpe26, 256x192 | [rtmpose-s_8xb1024-700e_body8-halpe26-256x192.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/body_2d_keypoint/rtmpose-s_8xb1024-700e_body8-halpe26-256x192.py) | [ONNX SDK zip](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-s_simcc-body7_pt-body7-halpe26_700e-256x192-7f134165_20230605.zip) |
| Modular hand: RTMPose-m Hand5 alpha, 256x256 | [rtmpose-m_8xb32-210e_coco-wholebody-hand-256x256.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/hand_2d_keypoint/rtmpose-m_8xb32-210e_coco-wholebody-hand-256x256.py) | [ONNX SDK zip](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-m_simcc-hand5_pt-aic-coco_210e-256x256-74fb594_20230320.zip) |
| Optional hand-box acquisition: RTMDet-nano hand alpha, 320x320 | [rtmdet_nano_320-8xb32_hand.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmdet/hand/rtmdet_nano_320-8xb32_hand.py) | [ONNX SDK zip](https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmdet_nano_8xb32-300e_hand-267f9c8f.zip) |
| Later quality comparator: RTMW-x, 256x192 | [rtmw-x_8xb704-270e_cocktail14-256x192.py](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/wholebody_2d_keypoint/rtmw-x_8xb704-270e_cocktail14-256x192.py) | [checkpoint](https://download.openmmlab.com/mmpose/v1/projects/rtmw/rtmw-x_simcc-cocktail14_pt-ucoco_270e-256x192-13a2546d_20231208.pth); export required |

Publisher table costs are respectively 0.9, 2.22, 0.70, 2.581, 0.31 and 13.1 GFLOPs. FLOPs are not measured latency. Its RTMPose-m wholebody row lists 13.50 ms ORT on i7-11700 and 4.00 ms TensorRT FP16 on GTX 1660 Ti at 256x192; these are model figures, not an eight-person complete SDK test. Its pipeline tables use detector intervals of five frames. No comparable target-device hand/wholebody end-to-end benchmark was established here. [Publisher tables](https://github.com/open-mmlab/mmpose/tree/main/projects/rtmpose).

The modular design requires eight body crops plus sixteen hand crops each output cycle: 240 body and 480 hand evaluations/second at the maximum load, before any hand detector. It also adds ROI association, left/right identity, crop quality and same-source-frame synchronization work. The existing COCO17 body plus hands would still lack explicit toes; Halpe26 addresses that schema gap. These are architectural costs inferred from the required participant count.

RTMW is a useful quality comparator, but defer it until the small model experiment establishes the performance envelope. The publisher's RTMW-l rows link x-named configs and the m row uses differently named checkpoint/ONNX stems. Do not guess compatible pairs from display labels: resolve the exact architecture and checkpoint load before any export. The x row above is a less ambiguous checkpoint/config pair, still unvalidated locally.

## Semantic mapping and validity

The [official COCO wholebody metadata](https://github.com/open-mmlab/mmpose/blob/main/configs/_base_/datasets/coco_wholebody.py) contains body 0-16, feet 17-22, face 23-90, left hand 91-111 and right hand 112-132. Proposed semantic mappings below require validation against the project's compatibility contract; they are not claims of identical Kinect anatomy.

| Required semantic point | Wholebody source or proposed mapping |
| --- | --- |
| Wrist left/right | Body 9/10; hand roots 91/112 are separate model estimates and need a documented reconciliation rule |
| Thumb endpoint left/right | Thumb4 95/116 |
| Handtip left/right | Middle-finger tip 103/124 as the explicitly chosen compatibility mapping |
| Palm / Hand left/right | Derived palm center from valid inferred hand root and MCP landmarks; label derived, not directly observed palm-center output |
| Foot endpoint left/right | Big toe 17/20 as a documented compatibility mapping; small toes 18/21 and heels 19/22 also exist |

A hand-only model uses [21-point hand metadata](https://github.com/open-mmlab/mmpose/blob/main/configs/_base_/datasets/coco_wholebody_hand.py). Confirm its indexing from the artifact metadata before implementing the same semantics. Require visibility/confidence of each constituent for a derived palm. If direct palm-center regression is mandatory rather than derivation from actual hand inference, these candidates need a new head/training target. Do not substitute the wrist.

Torso centers, clavicles and other missing compatibility joints require separately marked geometric derivations or invalid output. No listed 2D candidate establishes metric 3D or Kinect orientation quality. Retain original frame IDs and timestamps for every body/hand result; stale snapshots cannot be counted as updates.

## Export and runtime constraints

The [wholebody-m config](https://github.com/open-mmlab/mmpose/blob/main/projects/rtmpose/rtmpose/wholebody_2d_keypoint/rtmpose-m_8xb64-270e_coco-wholebody-256x192.py) specifies 133 outputs, top-down affine preprocessing, width/height `(192,256)`, SimCC split ratio 2, RGB conversion and channel normalization. The [MMDeploy SimCC ORT export config](https://github.com/open-mmlab/mmdeploy/blob/main/configs/mmpose/pose-detection_simcc_onnxruntime_dynamic.py) exports `simcc_x` / `simcc_y`, dynamic batch axes, and excludes maximum decoding from the graph. Input spatial dimensions are fixed by that config. Do not infer batch support from a downloaded zip name.

Experiment sequence: record pinned source commits and artifact hashes; run official reference inference without flip augmentation; inspect ONNX tensor/opset contracts; compare decoded ONNX output to reference; benchmark CPU reference and an explicitly configured GPU provider separately. Log actual provider assignment and CPU fallbacks. GPU use, CUDA dependencies and any FP16/TensorRT conversion are additional implementation/validation work, not proven by the existing CPU build. Hand256 export needs its own spatial shape and tensor checks.

ONNX remains canonical. RKNN is a derived deployment artifact. [Rockchip Toolkit2](https://github.com/airockchip/rknn-toolkit2) describes conversion on the development computer and C/C++ RKNN Runtime on the board. RK3588 is supported; this does not establish support for every RTMCC/RTMW graph. Gate deployment on successful conversion, operator placement, output accuracy, quantization calibration including small/occluded hands, runtime/driver compatibility and board inference. Check fixed shapes/batching and core-mask behavior rather than assuming them. A conversion failure is a feasibility result; do not silently move expensive unsupported operators to CPU and call the NPU path passed.

## RKNN evidence and limits

The inspected [official model zoo](https://github.com/airockchip/rknn_model_zoo) and [examples directory](https://github.com/airockchip/rknn_model_zoo/tree/main/examples) list YOLOv8 pose, but no RTMPose, RTMW or hand-landmark sample. This is evidence of an unverified conversion path, not proof those models cannot convert.

The zoo reports YOLOv8n-pose INT8 `[1,3,640,640]` at 55.9 FPS on RK3588 **single NPU core**, maximum NPU frequency, model inference only; preprocessing/postprocessing excluded. The table does not establish scene person count or Android complete-skeleton performance. Its body-only output cannot meet hands/foot semantics. It is a useful board/toolchain smoke test only, not the selected replacement. [Benchmark and restrictions](https://github.com/airockchip/rknn_model_zoo#model-performance-benchmarkfps).

## Acceptance and licensing checkpoints

Run 1, 2, 4, 6 and 8 actual visible-person cases on both identified devices. Record per-person and per-hand completed inference rates, source-frame skew, result age, latency percentiles, jitter, dropout, ID switches, device/provider/precision, camera resolution, hand pixel size, duration, thermal behavior and memory. Report raw and smoothed results separately. An eight-person fresh complete 30 FPS pass remains **unproven** on both platforms.

Code licenses are available in [MMPose LICENSE](https://github.com/open-mmlab/mmpose/blob/main/LICENSE) and [RKNN Model Zoo LICENSE](https://github.com/airockchip/rknn_model_zoo/blob/main/LICENSE). Apache-licensed repository code alone does not establish checkpoint, dataset, runtime redistribution or commercial clearance. The [COCO-WholeBody publisher terms](https://github.com/jin-s13/COCO-WholeBody) explicitly limit the dataset to research/non-commercial use and request contact for commercial annotation usage; their displayed license name and linked BY-NC URL should be resolved with the publisher. This is a concrete commercial-use diligence issue, not a conclusion about the legal status of all trained weights. Track [DWPose project terms](https://github.com/IDEA-Research/DWPose), all listed training datasets for the chosen weights, and Rockchip runtime/toolkit terms. This report provides provenance pointers, not legal clearance.

No currency price or engineering-duration estimate is justified by the inspected sources. Concrete experiment costs are export/reference setup, GPU provider setup, corpus capture/annotation, sixteen-hand ROI processing in the modular path, RKNN conversion/calibration and a physical RK3588 sustained test. Do not defer the conversion feasibility probe until after a large Android SDK implementation.
