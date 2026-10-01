# FP16 pack1 input candidate: closed at the numerical gate

The distinct `gpu-fp16packed-input16` diagnostic uses official GPU conversion
from the frozen FP32 input to FP16 pack1. Pinned ncnn source proves that the
first scalar convolution reads Half values when fp16_packed=true and
fp16_storage/arithmetic=false. This correct input representation differs from
the previously closed FP32-input packed16 experiment. No upstream allocation
patch, manual byte reinterpretation, CPU half cast, SDK integration or fallback
is involved. Existing runners and historical outputs remain unchanged.

Fresh API26 ARM64 build and independent code review passed. Root reran all
67 focused YOLO tests (7.118s) and architecture/public-surface maintenance
checks successfully. OOM guards are source-order checks, not device fault
injection. Binary/source/recipe identities are respectively:

```
7b7d84a13173d00f21ce76f9a45321b0d3333bd95929b44fb64d2211ee2fbb06
376dcbfec71c0ff58c39701862fef9520fb8af043b03deb439e74edc766821ab
f7c44417682c78de246b0b74d6016cbd4c38809962fd0523a87897af1d814346
```

Root executed the frozen eleven fixtures on actual device e7c07019:

```powershell
py -3 tools/models/ncnn/yolo_device_gate.py --adb "D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe" --serial e7c07019 --runner out/android-yolo/fp16-input-gate/runner-frozen --gpu-mode gpu-fp16packed-input16 --fixtures seven-416 one-416 seven-320 one-320 seven-640 one-640 empty-416 seven-square320 one-square320 seven-square416 one-square416
py -3 -m unittest discover -s tests/reference -p 'test_yolo*.py' -q
py -3 tools/maintenance/check_architecture_boundaries.py
```

All eleven executions succeeded and all 44 CPU/GPU output files are finite.
All 22 CPU output hashes exactly match the original historical oracle. Fresh
comparison recomputation equals every archived comparison, with actual typed
input/output logs and source/recipe/binary bindings checked.

**All eleven numerical goldens fail the frozen limits.** Every out0 maximum
error exceeds0.2, ranging from0.22897 to0.96309. The empty416 out1 also exceeds
the maximum limit (0.20549) and mean limit0.01 (0.011542). The validator and
thresholds were not changed. This failure is finite numerical disagreement,
not the earlier FP32-input representation mismatch or evidence of general
FP16 device incapability.

Decoded count/association/annotation checks independently pass9/11. Seven320
fails count/annotations and greedy association checks; seven-square320 fails
one score comparison. Seven640 has all seven actual left wrists valid and
above left shoulders, with maximum matched joint coordinate difference about
0.61 source pixels. That isolated semantic result cannot override the failed
numerical gate or certify temporal following.

The UTF8 LF closure index
`tools/models/ncnn/yolo_fp16_input_gate_evidence.json` has SHA256
`78eb531e6b546c45f0a72808b4be1baf7e62b7f7a33fe837b80d02124bdebcf9`.
It binds all eleven archives, actual options, input/output hashes, original CPU
oracle, unchanged limits and each separate raw/decoded gate. The frozen source
bytes remain in `out/android-yolo/fp16-input-gate/source-snapshot`; Git line
ending normalization must not be mistaken for the exact built-source identity.

The bounded candidate is **closed without repair or integration**. No speed
measurement, >=25/30 FPS pass, default promotion, main merge or Release is
claimed. The oracle is the same converted ncnn graph on CPU, not an original
PyTorch/export reference. Accepted640 FP32 remains the phone baseline; the
explicit512 evaluation remains below25 and has partial frames. YOLO-M3 stays
active and all existing synchronization, API, Tracker and Region contracts
remain unchanged.
