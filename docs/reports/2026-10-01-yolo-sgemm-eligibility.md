# FP32 SGEMM convolution candidate

The integrated640x384 route follows arms substantially better, but its normal
seven-person capture is15.32 fresh observations/s. The separate sampled stage
diagnostic places about86% of recorded elapsed time in extraction, including
ncnn internal GPU waits. This candidate changes only official ncnn convolution
selection: Winograd=false, SGEMM=true. Packing remains enabled; subgroup and all
FP16 options remain disabled, with FP32 pack1 tensor boundaries.

The original source-bound runner and its11 historical archives are unchanged.
The candidate has a separate explicit `gpu-fp32-sgemm` runner/source/recipe and
checks configured and post-load effective options. Independent review found
a missing allocation check after void `convert_packing`; round1 rejects an empty
or incorrectly shaped/packed/typed converted tensor before download and drains
recorded commands before rejection and tensor destruction. Structural guard
tests do not establish actual hardware out-of-memory behavior.

API26 ARM64 runner build passes. Final frozen runner SHA:
`d754708bcb687bcb5413de06ce075fba1781cbe5972d9a9c2cc425451ed6c73f`.
Source SHA:
`f5e2fbc7e545d0fea842038c797a7ca7cfe79e747067c91dcf93bb264d5f76bd`.
Recipe SHA:
`819a695b322fa62c50d7c06e0405c781ade94cff16e578d52720dd870b8fe4e1`.

```powershell
py -3 -m unittest tests.reference.test_yolo_sgemm_gate tests.reference.test_yolo_packed16_gate tests.reference.test_yolo_pose_gate -q
py -3 tools/models/ncnn/yolo_device_gate.py --adb "D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe" --serial e7c07019 --runner out/android-yolo/sgemm-gate/runner-frozen-round1 --gpu-mode gpu-fp32-sgemm --fixtures seven-416 one-416 seven-320 one-320 seven-640 one-640 empty-416 seven-square320 one-square320 seven-square416 one-square416
py -3 out/android-yolo/sgemm-gate/record_round1.py
```

Fresh30/30 focused tests pass. All11 fresh source-bound Snapdragon888 fixture
executions pass the unchanged numerical, person-count and annotation gates.
Maximum raw error is0.00019646, below the unchanged0.2 limit. Each CPU output
hash exactly matches its corresponding frozen historical oracle. In the actual
seven-640 frame1500 output, all7 anatomical left wrists and shoulders are valid
at confidence>=0.2 and within the source image, with every left wrist above its
shoulder. This is one-frame semantic eligibility, not temporal acceptance.

The evidence index `tools/models/ncnn/yolo_sgemm_gate_evidence.json` has SHA
`1cb1dec026bdc920371a73bfd6da1abbcb88fa6ae22130e0894838b6e69ab104` and binds
all11 archives, source/recipe/binary, input/output, CPU oracle and limits.
The earlier runner/archives are preserved separately; final eligibility refers
only to round1. Missing-ADB preflight failures executed no device inference and
were preserved separately, without being mistaken for numerical failures.

This proves offline FP32 SGEMM eligibility against the same converted ncnn graph
CPU oracle, not original PyTorch/export ground truth. SDK backend option binding,
the Unity GPU-AHB pipeline build, integrated FPS/age/coverage and physical
acceptance are pending. No standalone FPS benchmark, speed claim, automatic
fallback, Release or main merge is included.
