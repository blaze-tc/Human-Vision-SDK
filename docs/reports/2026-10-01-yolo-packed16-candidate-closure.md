# Packed16 candidate closure: rejected representation contract

The integrated640x384 FP32 baseline is15.32 fresh observations/s and the user
reports basic action following. Resolution and existing accuracy thresholds must
be preserved while improving throughput. A bounded, separately named offline
candidate attempted FP32 pack1 input/output with ncnn internal `fp16_packed=true`,
`fp16_storage=false`, `fp16_arithmetic=false`. It never changed runtime defaults.

All11 original single/seven/negative-control fixtures executed on e7c07019 and
failed the unchanged numerical/semantic gates. Seven640 raw maximum error is
about4.27e37. Correct shape/dtype metadata alone is insufficient to prove numeric
storage interpretation. These outputs are ineligible and must not enter runtime.

Independent review identified the pinned library's actual storage interpretation:
`out/ncnn-20260526/source/src/gpu.cpp` scalar `buffer_ld1` under fp16_packed uses
`unpackHalf2x16`, while `net.cpp` Vulkan layout conversion preserves FP32 RGB when
packing remains pack1. The first convolution's shader therefore reads the
candidate's explicit FP32 RGB bytes as half scalars. This is a grounded candidate
representation incompatibility, not proof the device inherently lacks FP16.

The separate runner preserves the original runner/source/recipe and historical
FP32 archive hash binding. Candidate actual options, input/output diagnostics,
runner/recipe/model/source/outputs are bound in each fresh execution record.
Runner SHA256:
`00789087e297e1c22533d379ff13de41737c0fc766698b23ce330f1fdba6d935`.
Source SHA256:
`4081f5ac4d67874336985101162d73d38919041861674cb4b11f8f6b22c7897b`.

Root command:

```powershell
py -3 tools/models/ncnn/yolo_device_gate.py --adb D:/Developer/2021.3.45f1/Editor/Data/PlaybackEngines/AndroidPlayer/SDK/platform-tools/adb.exe --serial e7c07019 --runner out/android-yolo/packed16-gate/runner-candidate/yolo_packed16_golden_runner --root out/android-yolo --gpu-mode gpu-fp32-packed16 --fixtures seven-640 one-640 empty-416 seven-416 one-416 seven-320 one-320 seven-square320 one-square320 seven-square416 one-square416
```

Independent spec compliance and quality review PASS for safe failure closure:
23/23 focused tests, all11 device bindings and comparisons independently
recomputed and exactly matched, original source/recipe unchanged. The candidate
eligibility result is FAIL0/11. Evidence index:
`tools/models/ncnn/yolo_packed16_gate_evidence.json`.
The complete reference suite has recorded unrelated environment limitations:
Python3.10 lacks `hashlib.file_digest` used by rectangle staging; Python3.13 lacks
ONNX dependencies. No full-suite success is claimed and no unrelated fix is made.

This bounded candidate is closed to repair/integration. Preserve the existing
FP32 runtime and investigate actual backend submission overhead. No standalone
FPS benchmark, threshold relaxation, automatic fallback, main merge or Release.
